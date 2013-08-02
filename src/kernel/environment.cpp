/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <algorithm>
#include <vector>
#include <limits>
#include "environment.h"
#include "exception.h"
#include "debug.h"

namespace lean {
constexpr unsigned uninit = std::numeric_limits<int>::max();

/** \brief Implementation of the Lean environment. */
struct environment::imp {
    std::vector<std::vector<unsigned>> m_uvar_distances;
    std::vector<level>                 m_uvars;

    /** \brief Return v - k. It throws an exception if there is a underflow. */
    static int sub(int v, unsigned k) {
        long long r = static_cast<long long>(v) - static_cast<long long>(k);
        if (r < std::numeric_limits<int>::min())
            throw exception("universe overflow");
        return static_cast<int>(r);
    }

    /** \brief Return v + k. It throws an exception if there is an overflow. */
    static int add(int v, unsigned k) {
        long long r = static_cast<long long>(v) + static_cast<long long>(k);
        if (r > std::numeric_limits<int>::max() - 1)
            throw exception("universe overflow");
        return static_cast<int>(r);
    }

    /** \brief Return v + k. It throws an exception if there is an overflow. */
    static unsigned add(unsigned v, unsigned k) {
        unsigned long long r = static_cast<unsigned long long>(v) + static_cast<unsigned long long>(k);
        if (r > std::numeric_limits<int>::max() - 1)
            throw exception("universe overflow");
        return static_cast<unsigned>(r);
    }

    /** \brief Return true iff l1 >= l2 + k */
    bool is_ge(level const & l1, level const & l2, int k) {
        switch (kind(l2)) {
        case level_kind::UVar:
            switch (kind(l1)) {
            case level_kind::UVar: {
                unsigned d = m_uvar_distances[uvar_idx(l1)][uvar_idx(l2)];
                return d != uninit && (k < 0 || (k >= 0 && d >= static_cast<unsigned>(k)));
            }
            case level_kind::Lift: return is_ge(lift_of(l1), l2, sub(k, lift_offset(l1)));
            case level_kind::Max:  return std::any_of(max_begin_levels(l1), max_end_levels(l1), [&](level const & l) { return is_ge(l, l2, k); });
            }
        case level_kind::Lift: return is_ge(l1, lift_of(l2), add(k, lift_offset(l2)));
        case level_kind::Max:  return std::all_of(max_begin_levels(l2), max_end_levels(l2), [&](level const & l) { return is_ge(l1, l, k); });
        }
        lean_unreachable();
        return false;
    }

    bool is_ge(level const & l1, level const & l2) {
        return is_ge(l1, l2, 0);
    }

    level add_var(name const & n) {
        if (std::any_of(m_uvars.begin(), m_uvars.end(), [&](level const & l){ return uvar_name(l) == n; }))
            throw exception("invalid universe variable declaration, it has already been declared");
        unsigned idx = m_uvars.size();
        level r(n, idx);
        m_uvars.push_back(r);
        std::for_each(m_uvar_distances.begin(), m_uvar_distances.end(), [](std::vector<unsigned> & v) { v.push_back(uninit); });
        m_uvar_distances.push_back(std::vector<unsigned>());
        std::vector<unsigned> & d = m_uvar_distances.back();
        d.resize(m_uvars.size(), static_cast<unsigned>(uninit));
        d[idx] = 0;
        return r;
    }

    void add_constraint(uvar v1, uvar v2, unsigned d) {
        lean_assert(v1 != v2);
        unsigned num = m_uvar_distances.size();
        lean_assert(v1 < num);
        lean_assert(v2 < num);
        std::vector<unsigned> & v1_dists = m_uvar_distances[v1];
        if (v1_dists[v2] == uninit || d >= v1_dists[v2]) {
            v1_dists[v2] = d;
            // update forward
            std::vector<unsigned> & v2_dists = m_uvar_distances[v2];
            for (uvar v3 = 0; v3 < num; v3++) {
                if (v2_dists[v3] != uninit) {
                    lean_assert(v1 != v3);
                    unsigned d_v1_v3 = add(d, v2_dists[v3]);
                    if (v1_dists[v3] == uninit || d_v1_v3 >= v1_dists[v3])
                        v1_dists[v3] = d_v1_v3;
                }
            }
        }
    }

    void add_constraints(uvar v1, level const & l, unsigned k) {
        switch (kind(l)) {
        case level_kind::UVar: add_constraint(v1, uvar_idx(l), k); return;
        case level_kind::Lift: add_constraints(v1, lift_of(l), add(k, lift_offset(l))); return;
        case level_kind::Max:  std::for_each(max_begin_levels(l), max_end_levels(l), [&](level const & l1) { add_constraints(v1, l1, k); }); return;
        }
        lean_unreachable();
    }

    level define_uvar(name const & n, level const & l) {
        level r = add_var(n);
        add_constraints(uvar_idx(r), l, 0);
        return r;
    }

    void init_uvars() {
        m_uvars.push_back(level());
        m_uvar_distances.push_back(std::vector<unsigned>());
        m_uvar_distances.back().push_back(0);
        lean_assert(uvar_idx(m_uvars.back()) == 0);
    }

    void display_uvars(std::ostream & out) const {
        std::for_each(m_uvars.begin(), m_uvars.end(),
                      [&](level const & u) {
                          std::vector<unsigned> const & u_dists = m_uvar_distances[uvar_idx(u)];
                          unsigned num = u_dists.size();
                          for (uvar v2 = 0; v2 < num; v2++) {
                              if (v2 != uvar_idx(u) && u_dists[v2] != uninit) {
                                  out << uvar_name(u) << " >= " << uvar_name(m_uvars[v2]);
                                  if (u_dists[v2] > 0)
                                      out << " + " << u_dists[v2];
                                  out << "\n";
                              }
                          }
                      });
    }

    imp() {
        init_uvars();
    }
};

environment::environment():
    m_imp(new imp) {
}

environment::~environment() {
}

level environment::define_uvar(name const & n, level const & l) {
    return m_imp->define_uvar(n, l);
}

bool environment::is_ge(level const & l1, level const & l2) const {
    return m_imp->is_ge(l1, l2);
}

void environment::display_uvars(std::ostream & out) const {
    m_imp->display_uvars(out);
}
}
