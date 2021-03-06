//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

/*
 * path_utils.hpp
 *
 */

#pragma once

#include "assembly_graph/paths/path_processor.hpp"

namespace debruijn_graph {

  // TODO: rewrite this function
  template<class Graph>
    vector<typename Graph::EdgeId> GetCommonPathsEnd(
        const Graph& g,
        typename Graph::EdgeId e1,
        typename Graph::EdgeId e2,
        size_t min_dist,
        size_t max_dist,
        const omnigraph::PathProcessor<Graph>& path_processor)
  {
      typedef typename Graph::EdgeId EdgeId;
      typedef vector<EdgeId> Path;

      //PathProcessor<Graph> path_processor(g,
                                          //min_dist - g.length(e1),
                                          //max_dist - g.length(e1),
          //g.EdgeEnd(e1), g.EdgeStart(e2), callback);

      omnigraph::PathStorageCallback<Graph> callback(g);
      int error_code = path_processor.Process(g.EdgeStart(e2), min_dist - g.length(e1),
                                              max_dist - g.length(e1), callback);
      vector<Path> paths = callback.paths();

      vector<EdgeId> result;
      if (error_code != 0) {
        DEBUG("Edge " << g.int_id(e1) << " path_processor problem")
        return result;
      }
      if (paths.size() == 0)
        return result;
      if (paths.size() == 1)
        return paths[0];
      size_t j = 0;
      while (j < paths[0].size()) {
        for (size_t i = 1;  i < paths.size(); ++i) {
          if (j == paths[i].size()) {
            vector<EdgeId> result(paths[0].begin()+(paths[0].size() - j), paths[0].end());
            return result;
          } else {
            if (paths[0][paths[0].size()-1-j] != paths[i][paths[i].size()-1-j]) {
              vector<EdgeId> result(paths[0].begin()+(paths[0].size() - j), paths[0].end());
              return result;
            }
          }
        }
        ++j;
      }
      return paths[0];
    }



  template<class Graph>
    vector<vector<typename Graph::EdgeId> > GetAllPathsBetweenEdges(
        const Graph& g,
        typename Graph::EdgeId& e1,
        typename Graph::EdgeId& e2, size_t min_dist,
        size_t max_dist) {
      omnigraph::PathStorageCallback<Graph> callback(g);
      ProcessPaths(g,
          min_dist,
          max_dist, //0, *cfg::get().ds.IS - K + size_t(*cfg::get().ds.is_var),
          g.EdgeEnd(e1), g.EdgeStart(e2),
          callback);
      auto paths = callback.paths();
      return paths;
    }

template<class graph_pack>
size_t GetAllPathsQuantity(const graph_pack& origin_gp,
                           const typename graph_pack::graph_t::EdgeId& e1,
                           const typename graph_pack::graph_t::EdgeId& e2, double d, double is_var) {
  omnigraph::PathStorageCallback<typename graph_pack::graph_t> callback(origin_gp.g);
  omnigraph::PathProcessor<typename graph_pack::graph_t>
      path_processor(origin_gp.g,
                     (size_t) d - origin_gp.g.length(e1) - size_t(is_var),
                     (size_t) d - origin_gp.g.length(e1) + size_t(is_var),
                     origin_gp.g.EdgeEnd(e1), 
                     origin_gp.g.EdgeStart(e2),
                     callback);
  path_processor.Process();
  auto paths = callback.paths();
  TRACE(e1.ind_id() << " " << e2.int_id() << " " << paths.size());
  return paths.size();
}

template<class Graph>
Sequence MergeSequences(const Graph& g,
                        const vector<typename Graph::EdgeId>& continuous_path) {
    vector < Sequence > path_sequences;
    path_sequences.push_back(g.EdgeNucls(continuous_path[0]));
    for (size_t i = 1; i < continuous_path.size(); ++i) {
        VERIFY(
                g.EdgeEnd(continuous_path[i - 1])
                == g.EdgeStart(continuous_path[i]));
        path_sequences.push_back(g.EdgeNucls(continuous_path[i]));
    }
    return MergeOverlappingSequences(path_sequences, g.k());
}

template<class Graph>
Sequence PathSequence(const Graph& g, const omnigraph::Path<typename Graph::EdgeId>& path) {
    Sequence path_sequence = MergeSequences(g, path.sequence());
    size_t start = path.start_pos();
    size_t end = path_sequence.size()
                 - g.length(path[path.size() - 1]) + path.end_pos();
    return path_sequence.Subseq(start, end);
}

}
