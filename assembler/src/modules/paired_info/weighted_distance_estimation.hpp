//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#ifndef WEIGHTED_DISTANCE_ESTIMATION_HPP_
#define WEIGHTED_DISTANCE_ESTIMATION_HPP_

#include "math/xmath.h"
#include "paired_info.hpp"
#include "distance_estimation.hpp"

namespace omnigraph {

namespace de {

template<class Graph>
class WeightedDistanceEstimator : public DistanceEstimator<Graph> {
protected:
    typedef DistanceEstimator<Graph> base;
    typedef typename base::InPairedIndex InPairedIndex;
    typedef typename base::OutPairedIndex OutPairedIndex;
    typedef typename base::InHistogram InHistogram;
    typedef typename base::OutHistogram OutHistogram;

public:
    WeightedDistanceEstimator(const Graph &graph,
                              const InPairedIndex &histogram,
                              const GraphDistanceFinder<Graph> &distance_finder,
                              std::function<double(int)> weight_f,
                              size_t linkage_distance, size_t max_distance) :
            base(graph, histogram, distance_finder, linkage_distance, max_distance), weight_f_(weight_f) { }

    virtual ~WeightedDistanceEstimator() { }

protected:
    typedef typename Graph::EdgeId EdgeId;

    typedef vector<pair<int, double> > EstimHist;
    typedef pair<EdgeId, EdgeId> EdgePair;
    typedef vector<size_t> GraphLengths;

    std::function<double(int)> weight_f_;

    virtual EstimHist EstimateEdgePairDistances(EdgePair ep,
                                                const InHistogram &histogram,
                                                const GraphLengths &raw_forward) const override {
        using std::abs;
        using namespace math;
        TRACE("Estimating with weight function");
        size_t first_len = this->graph().length(ep.first);
        size_t second_len = this->graph().length(ep.second);

        EstimHist result;
        int maxD = rounded_d(histogram.max()), minD = rounded_d(histogram.min());
        vector<int> forward;
        for (auto length : raw_forward) {
            if (minD - (int) this->max_distance_ <= length && length <= maxD + (int) this->max_distance_) {
                forward.push_back(length);
            }
        }
        if (forward.size() == 0)
            return result;

        DEDistance max_dist = this->max_distance_;
        size_t cur_dist = 0;
        vector<double> weights(forward.size());
        for (auto point : histogram) {
            if (ls(2. * point.d + (double) second_len, (double) first_len))
                continue;
            while (cur_dist + 1 < forward.size() && (double) forward[cur_dist + 1] < point.d) {
                ++cur_dist;
            }
            if (cur_dist + 1 < forward.size() && ls((double) forward[cur_dist + 1] - point.d,
                                                    point.d - (double) forward[cur_dist])) {
                ++cur_dist;
                if (le(abs(forward[cur_dist] - point.d), max_dist))
                    weights[cur_dist] += point.weight * weight_f_(forward[cur_dist] - rounded_d(point));
            }
            else if (cur_dist + 1 < forward.size() && eq(forward[cur_dist + 1] - point.d,
                                                         point.d - forward[cur_dist])) {
                if (le(abs(forward[cur_dist] - point.d), max_dist))
                    weights[cur_dist] += point.weight * 0.5 * weight_f_(forward[cur_dist] - rounded_d(point));

                ++cur_dist;

                if (le(abs(forward[cur_dist] - point.d), max_dist))
                    weights[cur_dist] += point.weight * 0.5 * weight_f_(forward[cur_dist] - rounded_d(point));
            } else if (le(abs(forward[cur_dist] - point.d), max_dist))
                weights[cur_dist] += point.weight * weight_f_(forward[cur_dist] - rounded_d(point));
        }

        for (size_t i = 0; i < forward.size(); ++i)
            if (gr(weights[i], 0.))
                result.push_back(make_pair(forward[i], weights[i]));

        return result;
    }

    const string Name() const override {
        static const string my_name = "WEIGHTED";
        return my_name;
    }

};

}

}
#endif
