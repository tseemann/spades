//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#include "pipeline/config_struct.hpp"

#include "pipeline/config_common.hpp"
#include "dev_support/openmp_wrapper.h"

#include "dev_support/logger/logger.hpp"
#include "dev_support/verify.hpp"

#include "io/reads_io/file_reader.hpp"

#include <string>
#include <vector>

#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"

using namespace llvm;

namespace io {
template<>
void SequencingLibrary<debruijn_graph::config::DataSetData>::yamlize(llvm::yaml::IO &io) {
  // First, load the "common stuff"
  SequencingLibraryBase::yamlize(io);

  // Now load the remaining stuff
  io.mapOptional("read length"                , data_.read_length);
  io.mapOptional("average read length"        , data_.avg_read_length);
  io.mapOptional("insert size mean"           , data_.mean_insert_size);
  io.mapOptional("insert size deviation"      , data_.insert_size_deviation);
  io.mapOptional("insert size left quantile"  , data_.insert_size_left_quantile);
  io.mapOptional("insert size right quantile" , data_.insert_size_right_quantile);
  io.mapOptional("insert size median"         , data_.median_insert_size);
  io.mapOptional("insert size mad"            , data_.insert_size_mad);
  io.mapOptional("insert size distribution"   , data_.insert_size_distribution);
  io.mapOptional("average coverage"           , data_.average_coverage);
  io.mapOptional("pi threshold"               , data_.pi_threshold);
  io.mapOptional("binary converted"           , data_.binary_coverted);
  io.mapOptional("single reads mapped"        , data_.single_reads_mapped);
}

template<>
void SequencingLibrary<debruijn_graph::config::DataSetData>::validate(llvm::yaml::IO &io, llvm::StringRef &res) {
  // Simply ask base class to validate for us
  SequencingLibraryBase::validate(io, res);
}
}

#include "pipeline/library.inl"

template class io::DataSet<debruijn_graph::config::DataSetData>;

namespace debruijn_graph {
namespace config {

template<typename mode_t>
vector<string> CheckedNames(const vector<pair<string, mode_t>>& mapping, mode_t total) {
    VERIFY_MSG(size_t(total) == mapping.size(), "Names for some modes missing")
    vector<string> answer;
    for (size_t i = 0; i < mapping.size(); ++i) {
        VERIFY_MSG(size_t(mapping[i].second) == i, "Id/name mapping error");
        answer.push_back(mapping[i].first);
    }
    return answer;
}

vector<string> InfoPrinterPosNames() {
    return CheckedNames<info_printer_pos>({
                    {"default", info_printer_pos::default_pos},
                    {"before_first_gap_closer", info_printer_pos::before_first_gap_closer},
                    {"before_simplification", info_printer_pos::before_simplification},
                    {"before_post_simplification", info_printer_pos::before_post_simplification},
                    {"final_simplified", info_printer_pos::final_simplified},
                    {"final_gap_closed", info_printer_pos::final_gap_closed},
                    {"before_repeat_resolution", info_printer_pos::before_repeat_resolution}}, info_printer_pos::total);
}

vector<string> PipelineTypeNames() {
    return CheckedNames<pipeline_type>({
                    {"base", pipeline_type::base},
                    {"isolate", pipeline_type::isolate},
                    {"mda", pipeline_type::mda},
                    {"meta", pipeline_type::meta},
                    {"moleculo", pipeline_type::moleculo},
                    {"diploid", pipeline_type::diploid},
                    {"rna", pipeline_type::rna},
                    {"plasmid", pipeline_type::plasmid}}, pipeline_type::total);
}

vector<string> ConstructionModeNames() {
    return CheckedNames<construction_mode>({
                    {"old", construction_mode::old},
                    {"extension", construction_mode::extention}}, construction_mode::total);
}

vector<string> EstimationModeNames() {
    return CheckedNames<estimation_mode>({
                    {"simple", estimation_mode::simple},
                    {"weighted", estimation_mode::weighted},
                    {"smoothing", estimation_mode::smoothing}}, estimation_mode::total);
}


vector<string> ResolveModeNames() {
    return CheckedNames<resolving_mode>({
                    {"none", resolving_mode::none},
                    {"path_extend", resolving_mode::path_extend}}, resolving_mode::total);
}

vector<string> SingleReadResolveModeNames() {
    return CheckedNames<single_read_resolving_mode>({
                    {"none", single_read_resolving_mode::none},
                    {"only_single_libs", single_read_resolving_mode::only_single_libs},
                    {"all", single_read_resolving_mode::all}}, single_read_resolving_mode::total);
}

void load_lib_data(const std::string& prefix) {
  // First, load the data into separate libs
  cfg::get_writable().ds.reads.load(prefix + ".lib_data");

  // Now, infer the common parameters
  size_t max_rl = 0;
  double avg_cov = 0.0;
  double avg_rl = 0.0;
  for (const auto& lib : cfg::get().ds.reads.libraries()) {
      auto const& data = lib.data();
      if (lib.is_graph_contructable())
          max_rl = std::max(max_rl, data.read_length);
      if (data.average_coverage > 0)
          avg_cov = data.average_coverage;
      if (data.avg_read_length > 0)
          avg_rl = data.avg_read_length;
  }

  cfg::get_writable().ds.set_RL(max_rl);
  cfg::get_writable().ds.set_aRL(avg_rl);
  cfg::get_writable().ds.set_avg_coverage(avg_cov);
}

void write_lib_data(const std::string& prefix) {
  cfg::get_writable().ds.reads.save(prefix + ".lib_data");
}

void load(debruijn_config::simplification::tip_clipper &tc,
          boost::property_tree::ptree const &pt, bool /*complete*/) {
     using config_common::load;
     load(tc.condition, pt, "condition");
}

void load(resolving_mode &rm, boost::property_tree::ptree const &pt,
          std::string const &key, bool complete) {
    if (complete || pt.find(key) != pt.not_found()) {
        rm = ModeByName<resolving_mode>(pt.get<std::string>(key), ResolveModeNames());
    }
}

void load(single_read_resolving_mode &rm, boost::property_tree::ptree const &pt,
          std::string const &key, bool complete) {
    if (complete || pt.find(key) != pt.not_found()) {
        std::string ep = pt.get<std::string>(key);
        rm = ModeByName<single_read_resolving_mode>(ep, SingleReadResolveModeNames());
    }
}

void load(construction_mode& con_mode,
          boost::property_tree::ptree const& pt, std::string const& key,
          bool complete) {
    if (complete || pt.find(key) != pt.not_found()) {
        con_mode = ModeByName<construction_mode>(pt.get<std::string>(key), ConstructionModeNames());
    }
}

void load(debruijn_config::construction::early_tip_clipper& etc,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
    using config_common::load;
    load(etc.enable, pt, "enable");
    etc.length_bound = pt.get_optional<size_t>("length_bound");
}

void load(debruijn_config::construction& con,
          boost::property_tree::ptree const& pt, bool complete) {
    using config_common::load;
    load(con.con_mode, pt, "mode", complete);
    load(con.keep_perfect_loops, pt, "keep_perfect_loops", complete);
    load(con.read_buffer_size, pt, "read_buffer_size", complete);
    con.read_buffer_size *= 1024 * 1024;
    load(con.early_tc, pt, "early_tip_clipper", complete);
}

void load(debruijn_config::sensitive_mapper& sensitive_map,
          boost::property_tree::ptree const& pt, bool complete) {
    using config_common::load;
    load(sensitive_map.k, pt, "k", complete);
}

void load(estimation_mode &est_mode,
          boost::property_tree::ptree const &pt, std::string const &key,
          bool complete) {
    if (complete || pt.find(key) != pt.not_found()) {
        est_mode = ModeByName<estimation_mode>(pt.get<std::string>(key), EstimationModeNames());
    }
}

void load(debruijn_config::simplification::bulge_remover& br,
          boost::property_tree::ptree const& pt, bool complete) {
  using config_common::load;

  load(br.enabled                           , pt,   "enabled"					  , complete);
  load(br.main_iteration_only               , pt,   "main_iteration_only"	      , complete);
  load(br.max_bulge_length_coefficient		, pt,   "max_bulge_length_coefficient", complete);
  load(br.max_additive_length_coefficient	, pt,
       "max_additive_length_coefficient", complete);
  load(br.max_coverage,                     pt,     "max_coverage", complete);
  load(br.max_relative_coverage,            pt,     "max_relative_coverage", complete);
  load(br.max_delta,                        pt,     "max_delta", complete);
  load(br.max_relative_delta,               pt,     "max_relative_delta", complete);
  load(br.max_number_edges,                 pt,     "max_number_edges", complete);
  load(br.parallel,                         pt,     "parallel", complete);
  load(br.buff_size,                        pt,     "buff_size", complete);
  load(br.buff_cov_diff,                    pt,     "buff_cov_diff", complete);
  load(br.buff_cov_rel_diff,                pt,     "buff_cov_rel_diff", complete);
}

void load(debruijn_config::simplification::topology_tip_clipper& ttc,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;
  load(ttc.length_coeff, pt, "length_coeff");
  load(ttc.plausibility_length, pt, "plausibility_length");
  load(ttc.uniqueness_length, pt, "uniqueness_length");
}

void load(debruijn_config::simplification::complex_tip_clipper &ctc,
          boost::property_tree::ptree const &pt, bool complete) {
    using config_common::load;
    load(ctc.enabled, pt, "enabled", complete);
    load(ctc.max_relative_coverage, pt, "max_relative_coverage", complete);
    load(ctc.max_edge_len, pt, "max_edge_len", complete);
    load(ctc.condition, pt, "condition", complete);
}

void load(debruijn_config::simplification::relative_coverage_edge_disconnector& relative_ed,
        boost::property_tree::ptree const& pt, bool complete) {
  using config_common::load;
  load(relative_ed.enabled, pt, "enabled", complete);
  load(relative_ed.diff_mult, pt, "diff_mult", complete);
}

void load(debruijn_config::simplification::relative_coverage_comp_remover& rcc,
          boost::property_tree::ptree const& pt, bool complete) {
  using config_common::load;
  load(rcc.enabled, pt, "enabled", complete);
  load(rcc.coverage_gap, pt, "coverage_gap", complete);
  load(rcc.length_coeff, pt, "max_length_coeff", complete);
  load(rcc.tip_allowing_length_coeff, pt, "max_length_with_tips_coeff", complete);
  load(rcc.vertex_count_limit, pt, "max_vertex_cnt", complete);
  load(rcc.max_ec_length_coefficient, pt, "max_ec_length_coefficient", complete);
  load(rcc.max_coverage_coeff, pt, "max_coverage_coeff", complete);
}

void load(debruijn_config::simplification::isolated_edges_remover& ier,
          boost::property_tree::ptree const& pt, bool complete) {
  using config_common::load;
  load(ier.enabled, pt, "enabled", complete);
  load(ier.max_length, pt, "max_length", complete);
  load(ier.max_coverage, pt, "max_coverage", complete);
  load(ier.max_length_any_cov, pt, "max_length_any_cov", complete);
}

void load(debruijn_config::simplification::init_cleaning& init_clean,
          boost::property_tree::ptree const& pt, bool complete) {
  using config_common::load;
  load(init_clean.self_conj_condition, pt, "self_conj_condition", complete);
  load(init_clean.early_it_only, pt, "early_it_only", complete);
  load(init_clean.activation_cov, pt, "activation_cov", complete);
  load(init_clean.ier, pt, "ier", complete);
  load(init_clean.tip_condition, pt, "tip_condition", complete);
  load(init_clean.ec_condition, pt, "ec_condition", complete);
  load(init_clean.disconnect_flank_cov, pt, "disconnect_flank_cov", complete);
}

void load(debruijn_config::simplification::complex_bulge_remover& cbr,
          boost::property_tree::ptree const& pt, bool complete) {
  using config_common::load;

  load(cbr.enabled, pt, "enabled");
  load(cbr.max_relative_length, pt, "max_relative_length", complete);
  load(cbr.max_length_difference, pt, "max_length_difference", complete);
}

void load(debruijn_config::simplification::erroneous_connections_remover& ec,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;

  load(ec.condition, pt, "condition");
}

void load(debruijn_config::simplification::topology_based_ec_remover& tec,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;

  load(tec.max_ec_length_coefficient, pt, "max_ec_length_coefficient");
  load(tec.plausibility_length, pt, "plausibility_length");
  load(tec.uniqueness_length, pt, "uniqueness_length");
}

void load(debruijn_config::simplification::interstrand_ec_remover &isec,
          boost::property_tree::ptree const &pt, bool /*complete*/) {
  using config_common::load;
  load(isec.max_ec_length_coefficient, pt, "max_ec_length_coefficient");
  load(isec.uniqueness_length, pt, "uniqueness_length");
  load(isec.span_distance, pt, "span_distance");
}

void load(debruijn_config::simplification::tr_based_ec_remover &trec,
          boost::property_tree::ptree const &pt, bool /*complete*/) {
  using config_common::load;
  load(trec.max_ec_length_coefficient, pt, "max_ec_length_coefficient");
  load(trec.unreliable_coverage, pt, "unreliable_coverage");
  load(trec.uniqueness_length, pt, "uniqueness_length");
}

void load(debruijn_config::simplification::max_flow_ec_remover& mfec,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;

  load(mfec.enabled, pt, "enabled");
  load(mfec.max_ec_length_coefficient, pt, "max_ec_length_coefficient");
  load(mfec.plausibility_length, pt, "plausibility_length");
  load(mfec.uniqueness_length, pt, "uniqueness_length");
}

void load(debruijn_config::simplification::hidden_ec_remover& her,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;

  load(her.enabled, pt, "enabled");
  load(her.uniqueness_length, pt, "uniqueness_length");
  load(her.unreliability_threshold, pt, "unreliability_threshold");
  load(her.relative_threshold, pt, "relative_threshold");
}

void load(debruijn_config::distance_estimator& de,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;

  load(de.linkage_distance_coeff, pt, "linkage_distance_coeff");
  load(de.max_distance_coeff, pt, "max_distance_coeff");
  load(de.max_distance_coeff_scaff, pt, "max_distance_coeff_scaff");
  load(de.filter_threshold, pt, "filter_threshold");
}

void load(debruijn_config::smoothing_distance_estimator& ade,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;

  load(ade.threshold, pt, "threshold");
  load(ade.range_coeff, pt, "range_coeff");
  load(ade.delta_coeff, pt, "delta_coeff");
  load(ade.percentage, pt, "percentage");
  load(ade.cutoff, pt, "cutoff");
  load(ade.min_peak_points, pt, "min_peak_points");
  load(ade.inv_density, pt, "inv_density");
  load(ade.derivative_threshold, pt, "derivative_threshold");
}

//FIXME make amb_de optional field
inline void load(debruijn_config::ambiguous_distance_estimator &amde,
                 boost::property_tree::ptree const &pt, bool complete) {
    using config_common::load;

    load(amde.enabled, pt, "enabled", complete);
    load(amde.haplom_threshold, pt, "haplom_threshold", complete);
    load(amde.relative_length_threshold, pt, "relative_length_threshold", complete);
    load(amde.relative_seq_threshold, pt, "relative_seq_threshold", complete);
}

void load(debruijn_config::scaffold_correction& sc_corr,
        boost::property_tree::ptree const& pt, bool /*complete*/) {
    using config_common::load;
    load(sc_corr.scaffolds_file, pt, "scaffolds_file");
    load(sc_corr.output_unfilled, pt, "output_unfilled");
    load(sc_corr.max_insert, pt, "max_insert");
    load(sc_corr.max_cut_length, pt, "max_cut_length");
}

void load(debruijn_config::truseq_analysis& tsa,
      boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;
  load(tsa.scaffolds_file, pt, "scaffolds_file");
  load(tsa.genome_file, pt, "genome_file");
}

void load(debruijn_config::bwa_aligner& bwa,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
    using config_common::load;
    load(bwa.bwa_enable, pt, "bwa_enable");
    load(bwa.debug, pt, "debug");
    load(bwa.path_to_bwa, pt, "path_to_bwa");
    load(bwa.min_contig_len, pt, "min_contig_len");
}

void load(debruijn_config::pacbio_processor& pb,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;
  load(pb.pacbio_k, pt, "pacbio_k");
  load(pb.additional_debug_info, pt, "additional_debug_info");
  load(pb.compression_cutoff, pt, "compression_cutoff");
  load(pb.domination_cutoff, pt, "domination_cutoff");
  load(pb.path_limit_stretching, pt, "path_limit_stretching");
  load(pb.path_limit_pressing, pt, "path_limit_pressing");
  load(pb.ignore_middle_alignment, pt, "ignore_middle_alignment");
  load(pb.long_seq_limit, pt, "long_seq_limit");
  load(pb.pacbio_min_gap_quantity, pt, "pacbio_min_gap_quantity");
  load(pb.contigs_min_gap_quantity, pt, "contigs_min_gap_quantity");
  load(pb.max_contigs_gap_length, pt, "max_contigs_gap_length");

}


void load(debruijn_config::position_handler& pos,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;
  load(pos.max_mapping_gap, pt, "max_mapping_gap");
  load(pos.max_gap_diff, pt, "max_gap_diff");
  load(pos.contigs_for_threading, pt, "contigs_for_threading");
  load(pos.contigs_to_analyze, pt, "contigs_to_analyze");
  load(pos.late_threading, pt, "late_threading");
  load(pos.careful_labeling, pt, "careful_labeling");
}
void load(debruijn_config::plasmid& pd,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
    using config_common::load;
    load(pd.long_edge_length, pt, "long_edge_length");
    load(pd.edge_length_for_median, pt, "edge_length_for_median");

    load(pd.relative_coverage, pt, "relative_coverage");
    load(pd.small_component_size, pt, "small_component_size");
    load(pd.small_component_relative_coverage, pt, "small_component_relative_coverage");
    load(pd.min_component_length, pt, "min_component_length");
    load(pd.min_isolated_length, pt, "min_isolated_length");

}


void load(debruijn_config::gap_closer& gc,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;
  load(gc.minimal_intersection, pt, "minimal_intersection");
  load(gc.before_simplify, pt, "before_simplify");
  load(gc.in_simplify, pt, "in_simplify");
  load(gc.after_simplify, pt, "after_simplify");
  load(gc.weight_threshold, pt, "weight_threshold");
}

void load(debruijn_config::graph_read_corr_cfg& graph_read_corr,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;
  load(graph_read_corr.enable, pt, "enable");
  load(graph_read_corr.output_dir, pt, "output_dir");
  load(graph_read_corr.binary, pt, "binary");
}

void load(debruijn_config::kmer_coverage_model& kcm,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;
  load(kcm.probability_threshold, pt, "probability_threshold");
  load(kcm.strong_probability_threshold, pt, "strong_probability_threshold");
  load(kcm.coverage_threshold, pt, "coverage_threshold");
  load(kcm.use_coverage_threshold, pt, "use_coverage_threshold");
}

void load(dataset &ds,
          boost::property_tree::ptree const &pt, bool /*complete*/) {
    using config_common::load;

  load(ds.reads_filename, pt, "reads");

    ds.reference_genome_filename = "";
    boost::optional<std::string> refgen =
            pt.get_optional<std::string>("reference_genome");
    if (refgen && *refgen != "N/A") {
        ds.reference_genome_filename = *refgen;
    }
}

void load_reads(dataset &ds,
                std::string input_dir) {
    if (ds.reads_filename[0] != '/')
        ds.reads_filename = input_dir + ds.reads_filename;
    path::CheckFileExistenceFATAL(ds.reads_filename);
    ds.reads.load(ds.reads_filename);
}

void load_reference_genome(dataset &ds,
                           std::string input_dir) {
    if (ds.reference_genome_filename == "") {
        ds.reference_genome = "";
        return;
    }
    if (ds.reference_genome_filename[0] != '/')
        ds.reference_genome_filename = input_dir + ds.reference_genome_filename;
    path::CheckFileExistenceFATAL(ds.reference_genome_filename);
    io::FileReadStream genome_stream(ds.reference_genome_filename);
    io::SingleRead genome;
    genome_stream >> genome;
    ds.reference_genome = genome.GetSequenceString();
}

void load(debruijn_config::simplification& simp,
          boost::property_tree::ptree const& pt, bool complete) {
  using config_common::load;

  load(simp.cycle_iter_count, pt, "cycle_iter_count", complete);
  load(simp.post_simplif_enabled, pt, "post_simplif_enabled", complete);
  load(simp.topology_simplif_enabled, pt, "topology_simplif_enabled", complete);
  load(simp.tc, pt, "tc", complete); // tip clipper:
  load(simp.ttc, pt, "ttc", complete); // topology tip clipper:
  load(simp.complex_tc, pt, "complex_tc", complete); // complex tip clipper:
  load(simp.br, pt, "br", complete); // bulge remover:
  load(simp.ec, pt, "ec", complete); // erroneous connections remover:
  load(simp.rcc, pt, "rcc", complete); // relative coverage component remover:
  load(simp.relative_ed, pt, "relative_ed", complete); // relative edge disconnector:
  load(simp.tec, pt, "tec", complete); // topology aware erroneous connections remover:
  load(simp.trec, pt, "trec", complete); // topology and reliability based erroneous connections remover:
  load(simp.isec, pt, "isec", complete); // interstrand erroneous connections remover (thorn remover):
  load(simp.mfec, pt, "mfec", complete); // max flow erroneous connections remover:
  load(simp.ier, pt, "ier", complete); // isolated edges remover
  load(simp.cbr, pt, "cbr", complete); // complex bulge remover
  load(simp.her, pt, "her", complete); // hidden ec remover
  load(simp.init_clean, pt, "init_clean", complete); // presimplification
  load(simp.final_tc, pt, "final_tc", complete);
  load(simp.final_br, pt, "final_br", complete);
  simp.second_final_br = simp.final_br; 
  load(simp.second_final_br, pt, "second_final_br", false);
}

void load(debruijn_config::info_printer& printer,
          boost::property_tree::ptree const& pt, bool complete) {
  using config_common::load;
  load(printer.basic_stats, pt, "basic_stats", complete);
  load(printer.lib_info, pt, "lib_info", complete);
  load(printer.extended_stats, pt, "extended_stats", complete);
  load(printer.write_components, pt, "write_components", complete);
  load(printer.components_for_kmer, pt, "components_for_kmer", complete);
  load(printer.components_for_genome_pos, pt, "components_for_genome_pos",
       complete);
  load(printer.write_components_along_genome, pt,
       "write_components_along_genome", complete);
  load(printer.write_components_along_contigs, pt,
       "write_components_along_contigs", complete);
  load(printer.save_full_graph, pt, "save_full_graph", complete);
  load(printer.write_full_graph, pt, "write_full_graph", complete);
  load(printer.write_full_nc_graph, pt, "write_full_nc_graph", complete);
  load(printer.write_error_loc, pt, "write_error_loc", complete);
}

//void clear(debruijn_config::info_printer& printer) {
//    printer.print_stats = false;
//    printer.write_components = false;
//    printer.components_for_kmer = "";
//    printer.components_for_genome_pos = "";
//    printer.write_components_along_genome = false;
//    printer.save_full_graph = false;
//    printer.write_full_graph = false;
//    printer.write_full_nc_graph = false;
//    printer.write_error_loc = false;
//}

void load(debruijn_config::info_printers_t &printers,
          boost::property_tree::ptree const &pt, bool /*complete*/) {
    using config_common::load;

    debruijn_config::info_printer def;
    load(def, pt, ModeName(info_printer_pos::default_pos, InfoPrinterPosNames()), true);

    for (size_t pos = size_t(info_printer_pos::default_pos) + 1; pos != size_t(info_printer_pos::total); ++pos) {
        debruijn_config::info_printer printer(def);
        load(printer, pt, ModeName(pos, InfoPrinterPosNames()), false);

        printers[info_printer_pos(pos)] = printer;
    }
}

void load_launch_info(debruijn_config &cfg, boost::property_tree::ptree const &pt) {
    using config_common::load;
    load(cfg.K, pt, "K");
    // input options:
    load(cfg.dataset_file, pt, "dataset");
    // input dir is based on dataset file location (all paths in datasets are relative to its location)
    cfg.input_dir = path::parent_path(cfg.dataset_file);
    if (cfg.input_dir[cfg.input_dir.length() - 1] != '/')
        cfg.input_dir += '/';

    load(cfg.output_base, pt, "output_base");
    if (cfg.output_base[cfg.output_base.length() - 1] != '/')
        cfg.output_base += '/';

    load(cfg.log_filename, pt, "log_filename");

    load(cfg.developer_mode, pt, "developer_mode");
    if (cfg.developer_mode) {
        load(cfg.output_pictures, pt, "output_pictures");
        load(cfg.output_nonfinal_contigs, pt, "output_nonfinal_contigs");
        load(cfg.compute_paths_number, pt, "compute_paths_number");
    } else {
        cfg.output_pictures = false;
        cfg.output_nonfinal_contigs = false;
        cfg.compute_paths_number = false;
    }

    load(cfg.load_from, pt, "load_from");
    if (cfg.load_from[0] != '/') { // relative path
        cfg.load_from = cfg.output_dir + cfg.load_from;
    }

    load(cfg.tmp_dir, pt, "tmp_dir");
    load(cfg.main_iteration, pt, "main_iteration");

    load(cfg.entry_point, pt, "entry_point");

    load(cfg.use_additional_contigs, pt, "use_additional_contigs");
    load(cfg.additional_contigs, pt, "additional_contigs");
    load(cfg.rr_enable, pt, "rr_enable");

    load(cfg.buffer_size, pt, "buffer_size");
    cfg.buffer_size <<= 20; //turn MB to bytes

    load(cfg.temp_bin_reads_dir, pt, "temp_bin_reads_dir");
    if (cfg.temp_bin_reads_dir[cfg.temp_bin_reads_dir.length() - 1] != '/')
        cfg.temp_bin_reads_dir += '/';

    load(cfg.max_threads, pt, "max_threads");
    // Fix number of threads according to OMP capabilities.
    cfg.max_threads = std::min(cfg.max_threads, (size_t) omp_get_max_threads());
    // Inform OpenMP runtime about this :)
    omp_set_num_threads((int) cfg.max_threads);

    load(cfg.max_memory, pt, "max_memory");

    path::CheckFileExistenceFATAL(cfg.dataset_file);
    boost::property_tree::ptree ds_pt;
    boost::property_tree::read_info(cfg.dataset_file, ds_pt);
    load(cfg.ds, ds_pt, true);
    load_reads(cfg.ds, cfg.input_dir);
    load_reference_genome(cfg.ds, cfg.input_dir);
}

// main debruijn config load function
void load_cfg(debruijn_config &cfg, boost::property_tree::ptree const &pt,
          bool complete) {
    using config_common::load;

    string mode_str = pt.get("mode", "");
    if (!mode_str.empty()) {
        cfg.mode = ModeByName<pipeline_type>(mode_str, PipelineTypeNames());
    }

    //FIXME
    load(cfg.tsa, pt, "tsa", complete);

    load(cfg.use_unipaths, pt, "use_unipaths", complete);

    load(cfg.pb, pt, "pacbio_processor", complete);

    load(cfg.two_step_rr, pt, "two_step_rr", complete);
    load(cfg.use_intermediate_contigs, pt, "use_intermediate_contigs", complete);
    load(cfg.single_reads_rr, pt, "single_reads_rr", complete);

    load(cfg.correct_mismatches, pt, "correct_mismatches", complete);
    load(cfg.paired_info_statistics, pt, "paired_info_statistics", complete);
    load(cfg.paired_info_scaffolder, pt, "paired_info_scaffolder", complete);
    load(cfg.gap_closer_enable, pt, "gap_closer_enable", complete);

    load(cfg.max_repeat_length, pt, "max_repeat_length", complete);

    load(cfg.est_mode, pt, "estimation_mode", complete);
    load(cfg.de, pt, "de", complete);
    load(cfg.ade, pt, "ade", complete); // advanced distance estimator:
    load(cfg.amb_de, pt, "amb_de", complete);

    load(cfg.con, pt, "construction", complete);
    load(cfg.gc, pt, "gap_closer", complete);
    load(cfg.simp, pt, "simp", complete);
    load(cfg.flanking_range, pt, "flanking_range", complete);
    load(cfg.graph_read_corr, pt, "graph_read_corr", complete);
    load(cfg.kcm, pt, "kmer_coverage_model", complete);
    load(cfg.pos, pt, "pos", complete); // position handler:

    load(cfg.rm, pt, "resolving_mode", complete);
    load(cfg.pe_params, pt, "pe", complete);

    load(cfg.use_scaffolder, pt, "use_scaffolder", complete);
    load(cfg.avoid_rc_connections, pt, "avoid_rc_connections", complete);

    load(cfg.sensitive_map, pt, "sensitive_mapper", complete);

    load(cfg.info_printers, pt, "info_printers", complete);

    load(cfg.bwa, pt, "bwa_aligner", complete);

    if (pt.count("plasmid")) {
        VERIFY_MSG(!cfg.pd, "Option can be loaded only once");
        cfg.pd.reset(debruijn_config::plasmid());
        load(*cfg.pd, pt, "plasmid");
    }

    if (pt.count("sc_cor")) {
        VERIFY_MSG(!cfg.sc_cor, "Option sc_cor can be loaded only once");
        cfg.sc_cor.reset(debruijn_config::scaffold_correction());
        load(*cfg.sc_cor, pt, "sc_cor");
    }

    if (pt.count("preliminary_simp")) {
        VERIFY_MSG(!cfg.preliminary_simp, "Option preliminary can be loaded only once");
        cfg.preliminary_simp.reset(cfg.simp);
        load(*cfg.preliminary_simp, pt, "preliminary_simp", false);
    }
    if (pt.count("prelim_pe")) {
        VERIFY_MSG(!cfg.prelim_pe_params, "Option prelim_pe can be loaded only once");
        cfg.prelim_pe_params.reset(cfg.pe_params);
        load(*cfg.prelim_pe_params, pt, "prelim_pe", false);
    }
}

void load(debruijn_config &cfg, const std::string &cfg_fns) {
    load(cfg, std::vector<std::string>({ cfg_fns }));
}

void load(debruijn_config &cfg, const std::vector<std::string> &cfg_fns) {
    VERIFY_MSG(cfg_fns.size() > 0, "Should provide at least one config file");
    boost::property_tree::ptree base_pt;
    boost::property_tree::read_info(cfg_fns[0], base_pt);

    load_launch_info(cfg, base_pt);
    load_cfg(cfg, base_pt, true);

    for (size_t i = 1 ; i < cfg_fns.size(); ++i) {
        boost::property_tree::ptree pt;
        boost::property_tree::read_info(cfg_fns[i], pt);

        //FIXME add logging of loading configs
        load_cfg(cfg, pt, false);
    }

    //some post-loading processing
    using config::pipeline_type;
    cfg.uneven_depth = set<pipeline_type>{pipeline_type::mda, pipeline_type::rna, pipeline_type::meta}.count(cfg.mode);
    if (!cfg.developer_mode) {
        cfg.pe_params.debug_output = false;
        cfg.pe_params.viz.DisableAll();
        cfg.pe_params.output.DisableAll();
    }

    if (!cfg.use_scaffolder) {
        cfg.pe_params.param_set.scaffolder_options.on = false;
    }
    cfg.need_mapping = cfg.developer_mode || cfg.correct_mismatches
                       || cfg.gap_closer_enable || cfg.rr_enable;

    cfg.output_dir = cfg.output_base + "/K" + ToString(cfg.K) + "/";

    cfg.output_saves = cfg.output_dir + "saves/";

    if (cfg.tmp_dir[0] != '/') { // relative path
        cfg.tmp_dir = cfg.output_dir + cfg.tmp_dir;
    }

    cfg.temp_bin_reads_path =
            cfg.project_name.empty() ?
            (cfg.output_base + "/" + cfg.temp_bin_reads_dir) :
            (cfg.output_base + cfg.project_name + "/"
             + cfg.temp_bin_reads_dir);
    cfg.temp_bin_reads_info = cfg.temp_bin_reads_path + "INFO";

    cfg.paired_read_prefix = cfg.temp_bin_reads_path + "_paired";
    cfg.single_read_prefix = cfg.temp_bin_reads_path + "_single";
}

}
}
