//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#pragma once

#include "utils/adt/iterator_range.hpp"
#include <boost/iterator/iterator_facade.hpp>
#include <btree/safe_btree_map.h>
#include <sparsehash/sparse_hash_map>

#include <type_traits>

#include "histogram.hpp"

namespace omnigraph {

namespace de {

/**
 * @brief Index of paired reads information. For each pair of edges, we store so-called histogram which is a set
 *        of points with distance between those edges. Index is internally arranged as a map of map of histograms:
 *        edge1 -> (edge2 -> histogram)
 *        When we add a point (a,b)->p into the index, we automatically insert a conjugate point (b',a')->p',
 *        (self-conjugate edge pairs are the sole exception), so the index is always conjugate-symmetrical.
 *        Index provides access for a lot of different information:
 *        - if you need to have a histogram between two edges, use Get(edge1, edge2);
 *        - if you need to get a neighbourhood of some edge (second edges with corresponding histograms), use Get(edge1);
 *        - if you need to skip a symmetrical half of that neighbourhood, use GetHalf(edge1);
 *        Backward information (e.g., (b,a)->-p) is currently inaccessible.
 * @param G graph type
 * @param Traits Policy-like structure with associated types of inner and resulting points, and how to convert between them
 * @param C map-like container type (parameterized by key and value type)
 */
template<typename G, typename Traits, template<typename, typename> class Container>
class PairedIndex {

private:
    typedef typename Traits::Gapped InnerPoint;
    typedef omnigraph::de::Histogram<InnerPoint> InnerHistogram;

public:
    typedef G Graph;

    typedef typename Traits::Expanded Point;
    typedef omnigraph::de::Histogram<Point> Histogram;
    typedef typename Graph::EdgeId EdgeId;
    typedef std::pair<EdgeId, EdgeId> EdgePair;

    typedef Container<EdgeId, InnerHistogram> InnerMap;
    typedef Container<EdgeId, InnerMap> StorageMap;

    typedef PairedIndex<G, Traits, Container> Self;

    //--Data access types--

    typedef typename StorageMap::const_iterator ImplIterator;

public:
    /**
     * @brief Smart proxy set representing a composite histogram of points between two edges.
     * @detail You can work with the proxy just like any constant set.
     *         The only major difference is that it returns all consisting points by value,
     *         because some of them don't exist in the underlying sets and are
     *         restored from the conjugate info on-the-fly.
     */
    class HistProxy {

    public:
        /**
         * @brief Iterator over a proxy set of points.
         */
        class Iterator: public boost::iterator_facade<Iterator, Point, boost::bidirectional_traversal_tag, Point> {

            typedef typename InnerHistogram::const_iterator InnerIterator;

        public:
            Iterator(InnerIterator iter, DEDistance offset, bool back = false)
                    : iter_(iter), offset_(offset), back_(back)
            {}

        private:
            friend class boost::iterator_core_access;

            Point dereference() const {
                auto i = iter_;
                if (back_) --i;
                Point result = Traits::Expand(*i, offset_);
                if (back_)
                    result.d = -result.d;
                return result;
            }

            void increment() {
                back_ ? --iter_ : ++iter_;
            }

            void decrement() {
                back_ ? ++iter_ : --iter_;
            }

            inline bool equal(const Iterator &other) const {
                return iter_ == other.iter_ && back_ == other.back_;
            }

            InnerIterator iter_; //current position
            DEDistance offset_; //edge length
            bool back_;
        };

        /**
         * @brief Returns a wrapper for a histogram.
         */
        HistProxy(const InnerHistogram& hist, DEDistance offset = 0, bool back = false)
            : hist_(hist), offset_(offset), back_(back)
        {}

        /**
         * @brief Returns an empty proxy (effectively a Null object pattern).
         */
        static const InnerHistogram& empty_hist() {
            static InnerHistogram res;
            return res;
        }

        /**
         * @brief Adds a point to the histogram.
         */
        //void insert(Point p) {
        //    hist_.insert(Traits::Shrink(p, offset_));
        //}

        Iterator begin() const {
            return Iterator(back_ ? hist_.end() : hist_.begin(), offset_, back_);
        }

        Iterator end() const {
            return Iterator(back_ ? hist_.begin() : hist_.end(), offset_, back_);
        }

        /**
         * @brief Finds the point with the minimal distance.
         */
        Point min() const {
            VERIFY(!empty());
            return *begin();
        }

        /**
         * @brief Finds the point with the maximal distance.
         */
        Point max() const {
            VERIFY(!empty());
            return *--end();
        }

        /**
         * @brief Returns the copy of all points in a simple flat histogram.
         */
        Histogram Unwrap() const {
            return Histogram(begin(), end());
        }

        size_t size() const {
            return hist_.size();
        }

        bool empty() const {
            return hist_.empty();
        }

    private:
        const InnerHistogram& hist_;
        DEDistance offset_;
        bool back_;
    };

    typedef typename HistProxy::Iterator HistIterator;

    //---- Traversing edge neighbours ----

    using EdgeHist = std::pair<EdgeId, HistProxy>;

    /**
     * @brief A proxy map representing neighbourhood of an edge,
     *        where `Key` is the graph edge ID and `Value` is the proxy histogram.
     * @detail You can work with the proxy just like with any constant map.
     *         The only major difference is that it returns all consisting pairs by value,
     *         because proxies are constructed on-the-fly.
     */
    class EdgeProxy {
    public:

        /**
         * @brief Iterator over a proxy map.
         * @detail For a full proxy, traverses both straight and conjugate pairs.
         *         For a half proxy, traverses only lesser pairs (i.e., (a,b) where (a,b)<=(b',a')) of edges.
         */
        class Iterator: public boost::iterator_facade<Iterator, EdgeHist, boost::forward_traversal_tag, EdgeHist> {

            typedef typename InnerMap::const_iterator InnerIterator;

            void Skip() { //For a half iterator, skip conjugate pairs
                while (half_ && iter_ != stop_ && index_.GreaterPair(edge_, iter_->first))
                    ++iter_;
            }

        public:
            Iterator(const PairedIndex &index, InnerIterator iter, InnerIterator stop, EdgeId edge, bool half)
                    : index_ (index)
                    , iter_(iter)
                    , stop_(stop)
                    , edge_(edge)
                    , half_(half)
            {
                Skip();
            }

            void increment() {
                ++iter_;
                Skip();
            }

            void operator=(const Iterator &other) {
                //TODO: is this risky without an assertion?
                //VERIFY(index_ == other.index_);
                //We shouldn't reassign iterators from one index onto another
                iter_ = other.iter_;
                stop_ = other.stop_;
                edge_ = other.edge_;
                half_ = other.half_;
            }

        private:
            friend class boost::iterator_core_access;

            bool equal(const Iterator &other) const {
                return iter_ == other.iter_;
            }

            EdgeHist dereference() const {
                const auto& hist = iter_->second;
                return std::make_pair(iter_->first, HistProxy(hist, index_.CalcOffset(edge_)));
            }

        private:
            const PairedIndex &index_; //TODO: get rid of this somehow
            InnerIterator iter_, stop_;
            EdgeId edge_;
            bool half_;
        };

        EdgeProxy(const PairedIndex &index, const InnerMap& map, EdgeId edge, bool half = false)
            : index_(index), map_(map), edge_(edge), half_(half)
        {}

        Iterator begin() const {
            return Iterator(index_, map_.begin(), map_.end(), edge_, half_);
        }

        Iterator end() const {
            return Iterator(index_, map_.end(), map_.end(), edge_, half_);
        }

        HistProxy operator[](EdgeId e2) const {
            if (half_ && index_.GreaterPair(edge_, e2))
                return HistProxy::empty_hist();
            return index_.Get(edge_, e2);
        }

        //Currently unused
        /*HistProxy<true> GetBack(EdgeId e2) const {
            return index_.GetBack(edge_, e2);
        }*/

        bool empty() const {
            return map_.empty();
        }

    private:
        const PairedIndex& index_;
        const InnerMap& map_;
        EdgeId edge_;
        //When false, represents all neighbours (consisting both of directly added data and "restored" conjugates).
        //When true, proxifies only half of the added edges.
        bool half_;
    };

    typedef typename EdgeProxy::Iterator EdgeIterator;

    //---------------- Constructor ----------------

    PairedIndex(const Graph &graph)
        : size_(0), graph_(graph)
    {}

public:
    /**
     * @brief Returns a conjugate pair for two edges.
     */
    EdgePair ConjugatePair(EdgeId e1, EdgeId e2) const {
        return std::make_pair(graph_.conjugate(e2), graph_.conjugate(e1));
    }
    /**
     * @brief Returns a conjugate pair for a pair of edges.
     */
    EdgePair ConjugatePair(EdgePair ep) const {
        return ConjugatePair(ep.first, ep.second);
    }

private:
    bool GreaterPair(EdgeId e1, EdgeId e2) const {
        auto ep = std::make_pair(e1, e2);
        return ep > ConjugatePair(ep);
    }

    void SwapConj(EdgeId &e1, EdgeId &e2) const {
        auto tmp = e1;
        e1 = graph_.conjugate(e2);
        e2 = graph_.conjugate(tmp);
    }

    size_t CalcOffset(EdgeId e) const {
        return this->graph().length(e);
    }

public:
    //---------------- Data inserting methods ----------------
    /**
     * @brief Adds a point between two edges to the index,
     *        merging weights if there's already one with the same distance.
     */
    void Add(EdgeId e1, EdgeId e2, Point p) {
        InnerPoint sp = Traits::Shrink(p, CalcOffset(e1));
        InsertWithConj(e1, e2, sp);
    }

    /**
     * @brief Adds a whole set of points between two edges to the index.
     */
    template<typename TH>
    void AddMany(EdgeId e1, EdgeId e2, const TH& hist) {
        for (auto p : hist) {
            InnerPoint sp = Traits::Shrink(p, CalcOffset(e1));
            InsertWithConj(e1, e2, sp);
        }
    }

private:

    void InsertWithConj(EdgeId e1, EdgeId e2, InnerPoint p) {
        size_ += storage_[e1][e2].merge_point(p);
        //TODO: deal with loops and self-conj
        SwapConj(e1, e2);
        size_ += storage_[e1][e2].merge_point(p);
    }

    bool IsSelfConj(EdgeId e1, EdgeId e2) {
        return e1 == graph_.conjugate(e2);
    }

public:
    /**
     * @brief Adds a lot of info from another index, using fast merging strategy.
     *        Should be used instead of point-by-point index merge.
     */
    template<class Index>
    void Merge(const Index& index_to_add) {
        auto& base_index = storage_;
        for (auto AddI = index_to_add.data_begin(); AddI != index_to_add.data_end(); ++AddI) {
            EdgeId e1_to_add = AddI->first;
            const auto& map_to_add = AddI->second;
            InnerMap& map_already_exists = base_index[e1_to_add];
            MergeInnerMaps(map_to_add, map_already_exists);
        }
        VERIFY(size() >= index_to_add.size());
    }

private:
    template<class OtherMap>
    void MergeInnerMaps(const OtherMap& map_to_add,
                        InnerMap& map) {
        for (const auto& to_add : map_to_add) {
            InnerHistogram& hist_exists = map[to_add.first];
            size_ += hist_exists.merge(to_add.second);
        }
    }

public:
    //---------------- Data deleting methods ----------------

    /**
     * @brief Removes the specific entry from the index, and its conjugate.
     * @warning Don't use it on unclustered index, because hashmaps require set_deleted_item
     * @return The number of deleted entries (0 if there wasn't such entry)
     */
    size_t Remove(EdgeId e1, EdgeId e2, Point p) {
        InnerPoint point = Traits::Shrink(p, graph_.length(e1));
        auto res = RemoveSingle(e1, e2, point);
        //TODO: deal with loops and self-conj
        SwapConj(e1, e2);
        res += RemoveSingle(e1, e2, point);
        return res;
    }

    /**
     * @brief Removes the whole histogram from the index, and its conjugate.
     * @warning Don't use it on unclustered index, because hashmaps require set_deleted_item
     * @return The number of deleted entries
     */
    size_t Remove(EdgeId e1, EdgeId e2) {
        auto res = RemoveAll(e1, e2);
        if (!IsSelfConj(e1, e2)) { //TODO: loops?
            SwapConj(e1, e2);
            res += RemoveAll(e1, e2);
        }
        return res;
    }

private:

    //TODO: remove duplicode
    size_t RemoveSingle(EdgeId e1, EdgeId e2, InnerPoint point) {
        auto i1 = storage_.find(e1);
        if (i1 == storage_.end())
            return 0;
        auto& map = i1->second;
        auto i2 = map.find(e2);
        if (i2 == map.end())
            return 0;
        InnerHistogram& hist = i2->second;
        if (!hist.erase(point))
           return 0;
        --size_;
        if (hist.empty()) { //Prune empty maps
            map.erase(e2);
            if (map.empty())
                storage_.erase(e1);
        }
        return 1;
    }

    size_t RemoveAll(EdgeId e1, EdgeId e2) {
        auto i1 = storage_.find(e1);
        if (i1 == storage_.end())
            return 0;
        auto& map = i1->second;
        auto i2 = map.find(e2);
        if (i2 == map.end())
            return 0;
        InnerHistogram& hist = i2->second;
        size_t size_decrease = hist.size();
        map.erase(i2);
        size_ -= size_decrease;
        if (map.empty()) //Prune empty maps
            storage_.erase(i1);
        return size_decrease;
    }

public:

    /**
     * @brief Removes all neighbourhood of an edge (all edges referring to it, and their histograms)
     * @warning To keep the symmetricity, it also deletes all conjugates, so the actual complexity is O(size).
     * @return The number of deleted entries
     */
    size_t Remove(EdgeId edge) {
        InnerMap &inner_map = storage_[edge];
        std::vector<EdgeId> to_remove;
        to_remove.reserve(inner_map.size());
        size_t old_size = this->size();
        for (const auto& ep : inner_map)
            to_remove.push_back(ep.first);
        for (auto e2 : to_remove)
            this->Remove(edge, e2);
        return old_size - this->size();
    }

    //---------------- Data accessing methods ----------------

    /**
     * @brief Underlying raw implementation data (for custom iterator helpers).
     */
    ImplIterator data_begin() const {
        return storage_.begin();
    }

    /**
     * @brief Underlying raw implementation data (for custom iterator helpers).
     */
    ImplIterator data_end() const {
        return storage_.end();
    }

    adt::iterator_range<ImplIterator> data() const {
        return adt::make_range(data_begin(), data_end());
    }

private:
    //When there is no such edge, returns a fake empty map for safety
    const InnerMap& GetImpl(EdgeId e) const {
        auto i = storage_.find(e);
        if (i != storage_.end())
            return i->second;
        return empty_map_;
    }

    //When there is no such histogram, returns a fake empty histogram for safety
    const InnerHistogram& GetImpl(EdgeId e1, EdgeId e2) const {
        auto i = storage_.find(e1);
        if (i != storage_.end()) {
            auto j = i->second.find(e2);
            if (j != i->second.end())
                return j->second;
        }
        return HistProxy::empty_hist();
    }

public:

    /**
     * @brief Returns a whole proxy map to the neighbourhood of some edge.
     * @param e ID of starting edge
     */
    EdgeProxy Get(EdgeId e) const {
        return EdgeProxy(*this, GetImpl(e), e);
    }

    /**
     * @brief Returns a half proxy map to the neighbourhood of some edge.
     * @param e ID of starting edge
     */
    EdgeProxy GetHalf(EdgeId e) const {
        return EdgeProxy(*this, GetImpl(e), e, true);
    }

    /**
     * @brief Operator alias of Get(id).
     */
    EdgeProxy operator[](EdgeId e) const {
        return Get(e);
    }

    /**
     * @brief Returns a histogram proxy for all points between two edges.
     */
    HistProxy Get(EdgeId e1, EdgeId e2) const {
        return HistProxy(GetImpl(e1, e2), CalcOffset(e1));
    }

    /**
     * @brief Operator alias of Get(e1, e2).
     */
    HistProxy operator[](EdgePair p) const {
        return Get(p.first, p.second);
    }

    //Currently unused
    /**
     * @brief Returns a backwards histogram proxy for all points between two edges.
     */
    /*HistProxy<true> GetBack(EdgeId e1, EdgeId e2) const {
        return HistProxy<true>(GetImpl(e2, e1), CalcOffset(e2));
    }*/

    /**
     * @brief Checks if an edge (or its conjugated twin) is consisted in the index.
     */
    bool contains(EdgeId edge) const {
        return storage_.count(edge) + storage_.count(graph_.conjugate(edge)) > 0;
    }

    /**
     * @brief Checks if there is a histogram for two points (or their conjugated pair).
     */
    bool contains(EdgeId e1, EdgeId e2) const {
        auto i1 = storage_.find(e1);
        if (i1 != storage_.end() && i1->second.count(e2))
            return true;
        return false;
    }

    //---------------- Miscellaneous ----------------

    /**
     * Returns the graph the index is based on. Needed for custom iterators.
     */
    const Graph &graph() const { return graph_; }

    /**
     * @brief Inits the index with graph data. For each edge, adds a loop with zero weight.
     * @warning Do not call this on non-empty indexes.
     */
    void Init() {
        //VERIFY(size() == 0);
        for (auto it = graph_.ConstEdgeBegin(); !it.IsEnd(); ++it)
            Add(*it, *it, Point());
    }

    /**
     * @brief Clears the whole index. Used in merging.
     */
    void Clear() {
        storage_.clear();
        size_ = 0;
    }

    /**
     * @brief Returns the physical index size (total count of all histograms).
     */
    size_t size() const { return size_; }

private:
    PairedIndex(size_t size, const Graph& graph, const StorageMap& storage)
        : size_(size), graph_(graph), storage_(storage) {}

public:
    /**
     * @brief Returns a copy of sub-index.
     * @deprecated Needed only in smoothing distance estimator.
     */
    Self SubIndex(EdgeId e1, EdgeId e2) const {
        InnerMap tmp;
        const auto& h1 = GetImpl(e1, e2);
        size_t size = h1.size();
        tmp[e1][e2] = h1;
        SwapConj(e1, e2);
        const auto& h2 = GetImpl(e1, e2);
        size += h2.size();
        tmp[e1][e2] = h2;
        return Self(size, graph_, tmp);
    };

private:
    size_t size_;
    const Graph& graph_;
    StorageMap storage_;
    InnerMap empty_map_; //null object
};

//Aliases for common graphs
template<typename K, typename V>
using safe_btree_map = btree::safe_btree_map<K, V>; //Two-parameters wrapper
template<typename Graph>
using PairedInfoIndexT = PairedIndex<Graph, PointTraits, safe_btree_map>;

template<typename K, typename V>
using sparse_hash_map = google::sparse_hash_map<K, V>; //Two-parameters wrapper
template<typename Graph>
using UnclusteredPairedInfoIndexT = PairedIndex<Graph, RawPointTraits, sparse_hash_map>;

/**
 * @brief A collection of paired indexes which can be manipulated as one.
 *        Used as a convenient wrapper in parallel index processing.
 */
template<class Index>
class PairedIndices {
    typedef std::vector<Index> Storage;
    Storage data_;

public:
    PairedIndices() {}

    PairedIndices(const typename Index::Graph& graph, size_t lib_num) {
        data_.reserve(lib_num);
        for (size_t i = 0; i < lib_num; ++i)
            data_.emplace_back(graph);
    }

    /**
     * @brief Initializes all indexes with zero points.
     */
    void Init() { for (auto& it : data_) it.Init(); }

    /**
     * @brief Clears all indexes.
     */
    void Clear() { for (auto& it : data_) it.Clear(); }

    Index& operator[](size_t i) { return data_[i]; }

    const Index& operator[](size_t i) const { return data_[i]; }

    size_t size() const { return data_.size(); }

    typename Storage::iterator begin() { return data_.begin(); }
    typename Storage::iterator end() { return data_.end(); }

    typename Storage::const_iterator begin() const { return data_.begin(); }
    typename Storage::const_iterator end() const { return data_.end(); }
};

template<class Graph>
using PairedInfoIndicesT = PairedIndices<PairedInfoIndexT<Graph>>;

template<class Graph>
using UnclusteredPairedInfoIndicesT = PairedIndices<UnclusteredPairedInfoIndexT<Graph>>;

template<typename K, typename V>
using unordered_map = std::unordered_map<K, V>; //Two-parameters wrapper
template<class Graph>
using PairedInfoBuffer = PairedIndex<Graph, RawPointTraits, unordered_map>;

template<class Graph>
using PairedInfoBuffersT = PairedIndices<PairedInfoBuffer<Graph>>;

}

}
