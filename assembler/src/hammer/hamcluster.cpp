//***************************************************************************
//* Copyright (c) 2011-2012 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

#include "hamcluster.hpp"

#include "globals.hpp"
#include "hammer_stats.hpp"
#include "mmapped_reader.hpp"
#include "union.hpp"

#include <iostream>
#include <sstream>

#ifdef USE_GLIBCXX_PARALLEL
#include <parallel/algorithm>
#endif

struct SubKMerComparator {
  bool operator()(const SubKMer &lhs, const SubKMer &rhs) {
    return Seq<K>::less2()(lhs.data, rhs.data);
  }
};

std::pair<size_t, size_t> SubKMerSplitter::split() {
  std::vector<SubKMer> data;

  MMappedReader ifs(ifname_, /* unlink */ true);
  std::ofstream ofs(ofname_, std::ios::out | std::ios::binary);
  VERIFY(ofs.good());
  size_t icnt = 0, ocnt = 0;
  while (ifs.good()) {
    SubKMerComparator comp;

    deserialize(data, ifs);

#ifdef USE_GLIBCXX_PARALLEL
    // Explicitly force a call to parallel sort routine.
    __gnu_parallel::sort(data.begin(), data.end(), comp);
#else
    std::sort(data.begin(), data.end(), comp);
#endif
    for (auto start = data.begin(), end = data.end(); start != end;) {
      auto chunk_end = std::upper_bound(start + 1, data.end(), *start, comp);
      serialize(ofs, start, chunk_end);
      start = chunk_end;
      ocnt += 1;
    }
    icnt += 1;
  }
  VERIFY(!ofs.fail());

  ofs.close();

  return make_pair(icnt, ocnt);
}

static unsigned hamdistKMer(hint_t x, hint_t y, unsigned tau) {
  unsigned dist = 0;
  for (unsigned i = 0; i < K; ++i) {
    if (Globals::blob[x + i] == 'N')
      continue;
    if (Globals::blob[y + i] == 'N')
      continue;
    if (Globals::blob[x + i] != Globals::blob[y + i]) {
      ++dist; if (dist > tau) return dist;
    }
  }
  return dist;
}

static bool canMerge(const unionFindClass &uf, int x, int y) {
  return (uf.set_size(x) + uf.set_size(y)) < 10000;
}


void processBlockQuadratic(unionFindClass &uf,
                           const std::vector<size_t> &block,
                           const std::vector<hint_t> &kmers,
                           unsigned tau) {
  size_t blockSize = block.size();
  for (size_t i = 0; i < blockSize; ++i) {
    uf.find_set(block[i]);
    for (uint32_t j = i + 1; j < blockSize; j++) {
      if (hamdistKMer(kmers[block[i]], kmers[block[j]], tau) <= tau &&
          canMerge(uf, block[i], block[j])) {
        uf.unionn(block[i], block[j]);
      }
    }
  }
}

void clusterMerge(unionFindClass &to, const unionFindClass &from) {
  std::vector<std::vector<int> > classes;

  from.get_classes(classes);
  for (size_t i = 0; i < classes.size(); ++i) {
    size_t first = classes[i][0];
    to.find_set(first);
    for (size_t j = 0; j < classes[i].size(); ++j) {
      to.unionn(first, classes[i][j]);
    }
  }
}

void KMerHamClusterer::cluster(const std::string &prefix,
                               const std::vector<hint_t> &kmers,
                               unionFindClass &uf) {
  // First pass - split & sort the k-mers
  std::ostringstream tmp;
  tmp << prefix << ".first";
  std::string fname(tmp.str());
  std::ofstream ofs(fname, std::ios::out | std::ios::binary);
  VERIFY(ofs.good());

  TIMEDLN("Serializing sub-kmers.");
  for (unsigned i = 0; i < tau_ + 1; ++i) {
    size_t from = (*Globals::subKMerPositions)[i];
    size_t to = (*Globals::subKMerPositions)[i+1];

    TIMEDLN("Serializing: [" << from << ", " << to << ")");
    serialize(ofs, kmers, NULL,
              SubKMerPartSerializer(from, to));
  }
  VERIFY(!ofs.fail());
  ofs.close();

  size_t big_blocks1 = 0;
  {
    TIMEDLN("Splitting sub-kmers, pass 1.");
    SubKMerSplitter Splitter(fname, fname + ".blocks");
    std::pair<size_t, size_t> stat = Splitter.split();
    TIMEDLN("Splitting done."
            " Processed " << stat.first << " blocks."
            " Produced " << stat.second << " blocks.");

    // Sanity check - there cannot be more blocks than tau + 1 times of total
    // kmer number. And on the first pass we have only tau + 1 input blocks!
    VERIFY(stat.first == tau_ + 1);
    VERIFY(stat.second <= (tau_ + 1) * kmers.size());

    // Ok, now in the files we have everything grouped in blocks in the output files.

    std::vector<size_t> block;

    TIMEDLN("Merge sub-kmers, pass 1");
    SubKMerBlockFile blocks(fname + ".blocks", /* unlink */ true);

    std::ostringstream tmp;
    tmp << prefix << ".second";
    fname = tmp.str();

    ofs.open(fname, std::ios::out | std::ios::binary);
    VERIFY(ofs.good());
    while (blocks.get_block(block)) {
      if (block.size() < cfg::get().subvectors_blocksize_quadratic_threshold) {
        // Merge small blocks.
        processBlockQuadratic(uf, block, kmers, tau_);
      } else {
        big_blocks1 += 1;
        // Otherwise - dump for next iteration.
        for (unsigned i = 0; i < tau_ + 1; ++i) {
          serialize(ofs, kmers, &block,
                    SubKMerStridedSerializer(i, tau_ + 1));
        }
      }
    }
    VERIFY(!ofs.fail());
    ofs.close();
    TIMEDLN("Merge done, total " << big_blocks1 << " new blocks generated.");
  }

  size_t big_blocks2 = 0;
  {
    TIMEDLN("Spliting sub-kmers, pass 2.");
    SubKMerSplitter Splitter(fname, fname + ".blocks");
    std::pair<size_t, size_t> stat = Splitter.split();
    TIMEDLN("Splitting done."
            " Processed " << stat.first << " blocks."
            " Produced " << stat.second << " blocks.");

    // Sanity check - there cannot be more blocks than tau + 1 times of total
    // kmer number. And there should be tau + 1 times big_blocks input blocks.
    VERIFY(stat.first == (tau_ + 1)*big_blocks1);
    VERIFY(stat.second <= (tau_ + 1) * (tau_ + 1) * kmers.size());

    TIMEDLN("Merge sub-kmers, pass 2");
    SubKMerBlockFile blocks(fname + ".blocks", /* unlink */ true);
    std::vector<size_t> block;

    size_t nblocks = 0;
    while (blocks.get_block(block)) {
      if (block.size() > 50) {
        big_blocks2 += 1;
#if 0
        for (size_t i = 0; i < block.size(); ++i) {
          std::string s(Globals::blob + kmers[block[i]], K);
          TIMEDLN("" << block[i] << ": " << s);
        }
#endif
      }
      processBlockQuadratic(uf, block, kmers, tau_);
      nblocks += 1;
    }
    TIMEDLN("Merge done, saw " << big_blocks2 << " big blocks out of " << nblocks << " processed.");
  }
}