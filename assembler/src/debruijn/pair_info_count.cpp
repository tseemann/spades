//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#include "standard.hpp"
#include "dataset_readers.hpp"
#include "read_converter.hpp"

#include "de/paired_info.hpp"

#include "utils.hpp"
#include "stats/debruijn_stats.hpp"

#include "is_counter.hpp"
#include "pair_info_count.hpp"
#include "sequence_mapper.hpp"
#include "short_read_mapper.hpp"
#include "long_read_mapper.hpp"
#include "pair_info_filler.hpp"
#include "stats/debruijn_stats.hpp"
#include "path_extend/split_graph_pair_info.hpp"

namespace debruijn_graph {
    typedef io::SequencingLibrary<debruijn_config::DataSetData> SequencingLib;


bool RefineInsertSizeForLib(conj_graph_pack& gp, size_t ilib, size_t edge_length_threshold) {

  INFO("Estimating insert size (takes a while)");
  InsertSizeCounter hist_counter(gp, edge_length_threshold, /* ignore negative */ true);
  SequenceMapperNotifier notifier(gp);
  notifier.Subscribe(ilib, &hist_counter);

  SequencingLib& reads = cfg::get_writable().ds.reads[ilib];
  VERIFY(reads.data().read_length != 0);
  auto paired_streams = paired_binary_readers(reads, false);
  notifier.ProcessLibrary(paired_streams, ilib, *ChooseProperMapper(gp, reads));

  INFO(hist_counter.mapped() << " paired reads (" <<
       ((double) hist_counter.mapped() * 100.0 / (double) hist_counter.total()) << "% of all) aligned to long edges");
  if (hist_counter.negative() > 3 * hist_counter.mapped())
      WARN("Too much reads aligned with negative insert size. Is the library orientation set properly?");
  if (hist_counter.mapped() == 0)
    return false;

  std::map<size_t, size_t> percentiles;
  hist_counter.FindMean(reads.data().mean_insert_size, reads.data().insert_size_deviation, percentiles);
  hist_counter.FindMedian(reads.data().median_insert_size, reads.data().insert_size_mad, reads.data().insert_size_distribution);
  if (reads.data().median_insert_size < gp.k_value + 2) {
    return false;
  }

  std::tie(reads.data().insert_size_left_quantile, reads.data().insert_size_right_quantile) = omnigraph::GetISInterval(0.8, reads.data().insert_size_distribution);

  return !reads.data().insert_size_distribution.empty();
}

void ProcessSingleReads(conj_graph_pack& gp, size_t ilib,
                        bool use_binary = true) {
    const SequencingLib& reads = cfg::get().ds.reads[ilib];
    SequenceMapperNotifier notifier(gp);
    SimpleLongReadMapper read_mapper(gp, gp.single_long_reads[ilib]);
    notifier.Subscribe(ilib, &read_mapper);

    auto mapper_ptr = ChooseProperMapper(gp, reads);
    if (use_binary) {
        auto single_streams = single_binary_readers(reads, false, true);
        notifier.ProcessLibrary(single_streams, ilib, *mapper_ptr);
    } else {
        auto single_streams = single_easy_readers(reads, false,
                                                 true, /*handle Ns*/false);
        notifier.ProcessLibrary(single_streams, ilib, *mapper_ptr);
    }
    cfg::get_writable().ds.reads[ilib].data().single_reads_mapped = true;
}

void ProcessPairedReads(conj_graph_pack& gp, size_t ilib, bool map_single_reads) {
    const SequencingLib& reads = cfg::get().ds.reads[ilib];
    bool calculate_threshold = (reads.type() == io::LibraryType::PairedEnd);
    SequenceMapperNotifier notifier(gp);
    INFO("Left insert size qauntile " << reads.data().insert_size_left_quantile << ", right insert size quantile " << reads.data().insert_size_right_quantile);

    SimpleLongReadMapper read_mapper(gp, gp.single_long_reads[ilib]);
    if (map_single_reads) {
        notifier.Subscribe(ilib, &read_mapper);
    }

    path_extend::SplitGraphPairInfo split_graph(
            gp, (size_t) reads.data().median_insert_size,
            (size_t) reads.data().insert_size_deviation,
            (size_t) reads.data().insert_size_left_quantile,
            (size_t) reads.data().insert_size_right_quantile,
            reads.data().read_length, gp.g.k(),
            cfg::get().pe_params.param_set.split_edge_length,
            reads.data().insert_size_distribution);
    if (calculate_threshold) {
        notifier.Subscribe(ilib, &split_graph);
    }

    LatePairedIndexFiller pif(gp.g, PairedReadCountWeight, gp.paired_indices[ilib]);
    notifier.Subscribe(ilib, &pif);

    auto paired_streams = paired_binary_readers(reads, false, (size_t) reads.data().mean_insert_size);
    notifier.ProcessLibrary(paired_streams, ilib, *ChooseProperMapper(gp, reads));
    cfg::get_writable().ds.reads[ilib].data().pi_threshold = split_graph.GetThreshold();

    if (map_single_reads) {
        ProcessSingleReads(gp, ilib);
    }
}

bool HasGoodRRLibs() {
    for (size_t i = 0; i < cfg::get().ds.reads.lib_count(); ++i) {
        const auto& lib = cfg::get().ds.reads[i];
        if (lib.is_contig_lib())
            continue;
        if (lib.is_paired() &&
                lib.data().mean_insert_size == 0.0) {
            continue;
        }
        if (lib.is_repeat_resolvable()) {
            return true;
        }
    }
    return false;
}

bool HasOnlyMP() {
    for (size_t i = 0; i < cfg::get().ds.reads.lib_count(); ++i) {
        if (cfg::get().ds.reads[i].type() == io::LibraryType::PathExtendContigs)
            continue;
        if (cfg::get().ds.reads[i].type() != io::LibraryType::MatePairs && cfg::get().ds.reads[i].type() != io::LibraryType::HQMatePairs) {
            return false;
        }
    }
    return true;
}

//todo improve logic
bool ShouldMapSingleReads(size_t ilib) {
    switch (cfg::get().single_reads_rr) {
        case sr_none: {
            return false;
        }
        case sr_all: {
            return true;
        }
        case sr_only_single_libs: {
            //Map when no PacBio/paried libs or only mate-pairs or single lib itself
            return !HasGoodRRLibs() || HasOnlyMP() || (cfg::get().ds.reads[ilib].type() == io::LibraryType::SingleReads);
        }
    }
    return false;
}

void PairInfoCount::run(conj_graph_pack &gp, const char*) {
    gp.InitRRIndices();
    gp.EnsureBasicMapping();

    //fixme implement better universal logic 
    size_t edge_length_threshold = cfg::get().ds.meta ? 1000 : stats::Nx(gp.g, 50);
    INFO("Min edge length for estimation: " << edge_length_threshold);
    for (size_t i = 0; i < cfg::get().ds.reads.lib_count(); ++i) {
        INFO("Estimating insert size for library #" << i);
        const auto& lib = cfg::get().ds.reads[i];
        const auto& lib_data = lib.data();
        size_t rl = lib_data.read_length;
        size_t k = cfg::get().K;
        if (lib.is_paired()) {

            bool insert_size_refined = RefineInsertSizeForLib(gp, i, edge_length_threshold);

            if (!insert_size_refined) {
                cfg::get_writable().ds.reads[i].data().mean_insert_size = 0.0;
                WARN("Unable to estimate insert size for paired library #" << i);
                if (rl > 0 && rl <= k) {
                    WARN("Maximum read length (" << rl << ") should be greater than K (" << k << ")");
                } else if (rl <= k * 11 / 10) {
                    WARN("Maximum read length (" << rl << ") is probably too close to K (" << k << ")");
                } else {
                    WARN("None of paired reads aligned properly. Please, check orientation of your read pairs.");
                }
                continue;
            } else {
                INFO("  Estimated insert size for paired library #" << i);
                INFO("  Insert size = " << lib_data.mean_insert_size <<
                        ", deviation = " << lib_data.insert_size_deviation <<
                        ", left quantile = " << lib_data.insert_size_left_quantile <<
                        ", right quantile = " << lib_data.insert_size_right_quantile <<
                        ", read length = " << lib_data.read_length);

                if (lib_data.mean_insert_size < 1.1 * (double) rl) {
                    WARN("Estimated mean insert size " << lib_data.mean_insert_size
                                << " is very small compared to read length " << rl);
                }
            }
        }
    }

    for (size_t i = 0; i < cfg::get().ds.reads.lib_count(); ++i) {
        INFO("Mapping library #" << i);
        const auto& lib = cfg::get().ds.reads[i];
        if (lib.is_contig_lib() && !lib.is_pacbio_alignable()) {
            INFO("Mapping contigs library");
            ProcessSingleReads(gp, i, false);
		} else {
            bool map_single_reads = ShouldMapSingleReads(i);
            cfg::get_writable().use_single_reads |= map_single_reads;

            if (lib.is_paired() && lib.data().mean_insert_size != 0.0) {
                INFO("Mapping paired reads (takes a while) ");
                ProcessPairedReads(gp, i, map_single_reads);
            } else if (map_single_reads) {
                INFO("Mapping single reads (takes a while) ");
                ProcessSingleReads(gp, i);
            } 

            if (map_single_reads) {
                INFO("Total paths obtained from single reads: " << gp.single_long_reads[i].size());
            }
        }

    }

    SensitiveReadMapper<Graph>::EraseIndices();
}

}
