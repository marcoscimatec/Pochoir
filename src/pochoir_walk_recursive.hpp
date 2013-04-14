/*
 **********************************************************************************
 *  Copyright (C) 2010-2011  Massachusetts Institute of Technology
 *  Copyright (C) 2010-2011  Yuan Tang <yuantang@csail.mit.edu>
 *                           Charles E. Leiserson <cel@mit.edu>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Suggestsions:                  yuantang@csail.mit.edu
 *   Bugs:                          yuantang@csail.mit.edu
 *
 *********************************************************************************
 */

#ifndef POCHOIR_WALK_RECURSIVE_HPP
#define POCHOIR_WALK_RECURSIVE_HPP

#include "pochoir_common.hpp"
#include "pochoir_walk.hpp"

#define initial_cut(i) (lb[i] == phys_length_[i])
/* grid.x1[i] >= phys_grid_.x1[i] - stride_[i] - slope_[i] 
 * because we compute the kernel with range [a, b)
 */
template <int N_RANK>
inline bool Algorithm<N_RANK>::touch_boundary(int i, int lt, Grid_Info<N_RANK> & grid) 
{
    bool interior = false;
    if (grid.x0[i] >= uub_boundary[i] 
     && grid.x0[i] + grid.dx0[i] * lt >= uub_boundary[i]) {
#if (KLEIN == 0)
#if 1
        /* this is for NON klein bottle */
        interior = true;
        /* by this branch, we are assuming the shape is NOT a Klein bottle */
        grid.x0[i] -= phys_length_[i];
        grid.x1[i] -= phys_length_[i];
#else
        interior = false;
#endif
#else
        /* this is for klein bottle! */
#if 1
        interior = true;
        klein_region(grid, phys_grid_);
#else
        interior = false;
#endif
#endif
    } else if (grid.x1[i] <= ulb_boundary[i] 
            && grid.x1[i] + grid.dx1[i] * lt <= ulb_boundary[i]
            && grid.x0[i] >= lub_boundary[i]
            && grid.x0[i] + grid.dx0[i] * lt >= lub_boundary[i]) {
        interior = true;
    } else {
        interior = false;
    }
    return !interior;
}

template <int N_RANK>
inline bool Algorithm<N_RANK>::within_boundary(int t0, int t1, Grid_Info<N_RANK> & grid)
{
    bool l_touch_boundary = false;
    int lt = t1 - t0;
    for (int i = 0; i < N_RANK; ++i) {
        l_touch_boundary = l_touch_boundary || touch_boundary(i, lt, grid);
    }
    return !l_touch_boundary;
}

/* assuming that 'walk_serial' will only be called for interior region,
 * because boundary region usually it's very small, so let's call loop
 */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::walk_serial(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
    int lt = t1 - t0;
    bool cut_yet = false;
    bool can_cut[N_RANK];
    Grid_Info<N_RANK> l_grid;

    for (int i = 0; i < N_RANK; ++i) {
        can_cut[i] = (2 * (grid.x1[i] - grid.x0[i]) + (grid.dx1[i] - grid.dx0[i]) * lt >= 4 * slope_[i] * lt) && (grid.x1[i] - grid.x0[i] > dx_recursive_[i]);
        /* if all lb[i] < thres[i] && lt <= dt_recursive, 
           we have nothing to cut!
         */
    }

	/* N_RANK-1 because we exclude the time dimension here */
    for (int i = N_RANK-1; i >= 0 && !cut_yet; --i) {
        if (can_cut[i]) {
            l_grid = grid;
            int xm = (2 * (grid.x0[i] + grid.x1[i]) + (2 * slope_[i] + grid.dx0[i] + grid.dx1[i]) * lt) / 4;
            l_grid.x0[i] = grid.x0[i]; l_grid.dx0[i] = grid.dx0[i];
            l_grid.x1[i] = xm; l_grid.dx1[i] = -slope_[i];
            walk_serial(t0, t1, l_grid, f);
            l_grid.x0[i] = xm; l_grid.dx0[i] = -slope_[i];
            l_grid.x1[i] = grid.x1[i]; l_grid.dx1[i] = grid.dx1[i];
            walk_serial(t0, t1, l_grid, f);
            cut_yet = true;
        }/* end if */
    } /* end for spatial dimensions */
    if (lt > dt_recursive_) {
        int halflt = lt / 2;
        l_grid = grid;
        walk_serial(t0, t0+halflt, l_grid, f);

        for (int i = 0; i < N_RANK; ++i) {
            l_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
            l_grid.dx0[i] = grid.dx0[i];
            l_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
            l_grid.dx1[i] = grid.dx1[i];
        }
        walk_serial(t0+halflt, t1, l_grid, f);
        cut_yet = true;
    }
    base_case_kernel_boundary(t0, t1, grid, f);
    return;
}

template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::walk_bicut(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
	/* for the initial cut on each dimension, cut into exact N_CORES pieces,
	   for the rest cut into that dimension, cut into as many as we can!
	 */
	int lt = t1 - t0;
	index_info lb, thres;
	Grid_Info<N_RANK> l_grid;

	for (int i = 0; i < N_RANK; ++i) {
		lb[i] = grid.x1[i] - grid.x0[i];
		thres[i] = 2 * (2 * slope_[i] * lt);
	}	

	for (int i = N_RANK-1; i >= 0; --i) {
		if (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]) { 
			l_grid = grid;
			const int sep = (int)lb[i]/2;
#if DEBUG
//			printf("initial_cut = %s, lb[%d] = %d, sep = %d, r = %d\n", initial_cut(i) ? "True" : "False", i, lb[i], sep, r);
#endif
			l_grid.x0[i] = grid.x0[i];
			l_grid.dx0[i] = slope_[i];
			l_grid.x1[i] = grid.x0[i] + sep;
			l_grid.dx1[i] = -slope_[i];
			cilk_spawn walk_bicut(t0, t1, l_grid, f);

			l_grid.x0[i] = grid.x0[i] + sep;
			l_grid.dx0[i] = slope_[i];
			l_grid.x1[i] = grid.x1[i];
			l_grid.dx1[i] = -slope_[i];
			cilk_spawn walk_bicut(t0, t1, l_grid, f);
#if DEBUG
//			print_sync(stdout);
#endif
			cilk_sync;
			if (grid.dx0[i] != slope_[i]) {
				l_grid.x0[i] = grid.x0[i]; l_grid.dx0[i] = grid.dx0[i];
				l_grid.x1[i] = grid.x0[i]; l_grid.dx1[i] = slope_[i];
				cilk_spawn walk_bicut(t0, t1, l_grid, f);
			}

			l_grid.x0[i] = grid.x0[i] + sep;
			l_grid.dx0[i] = -slope_[i];
			l_grid.x1[i] = grid.x0[i] + sep;
			l_grid.dx1[i] = slope_[i];
			cilk_spawn walk_bicut(t0, t1, l_grid, f);

			if (grid.dx1[i] != -slope_[i]) {
				l_grid.x0[i] = grid.x1[i]; l_grid.dx0[i] = -slope_[i];
				l_grid.x1[i] = grid.x1[i]; l_grid.dx1[i] = grid.dx1[i];
				cilk_spawn walk_bicut(t0, t1, l_grid, f);
			}
#if DEBUG
			printf("%s:%d cut into %d dim\n", __FUNCTION__, __LINE__, i);
			fflush(stdout);
#endif
            return;
		}/* end if */
	} /* end for */
	if (lt > dt_recursive_) {
		int halflt = lt / 2;
		l_grid = grid;
		walk_bicut(t0, t0+halflt, l_grid, f);
#if DEBUG
//		print_sync(stdout);
#endif
		for (int i = 0; i < N_RANK; ++i) {
			l_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
			l_grid.dx0[i] = grid.dx0[i];
			l_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
			l_grid.dx1[i] = grid.dx1[i];
		}
		walk_bicut(t0+halflt, t1, l_grid, f);
#if DEBUG
//		printf("%s:%d cut into time dim\n", __FUNCTION__, __LINE__);
		fflush(stdout);
#endif
        return;
	}
    /* base case */
#if DEBUG
//    printf("call Adaptive! ");
//	  print_grid(stdout, t0, t1, grid);
#endif
	base_case_kernel_interior(t0, t1, grid, f);
	return;
}

#define push_queue(_dep, _level, _t0, _t1, _grid) \
do { \
    assert(queue_len_[_dep] < ALGOR_QUEUE_SIZE); \
    circular_queue_[_dep][queue_tail_[_dep]].level = _level; \
    circular_queue_[_dep][queue_tail_[_dep]].t0 = _t0; \
    circular_queue_[_dep][queue_tail_[_dep]].t1 = _t1; \
    circular_queue_[_dep][queue_tail_[_dep]].grid = _grid; \
    ++queue_len_[_dep]; \
    queue_tail_[_dep] = pmod((queue_tail_[_dep] + 1), ALGOR_QUEUE_SIZE); \
} while(0)

#define top_queue(_dep, _queue_elem) \
do { \
    assert(queue_len_[_dep] > 0); \
    _queue_elem = &(circular_queue_[_dep][queue_head_[_dep]]); \
} while(0)

#define pop_queue(_dep) \
do { \
    assert(queue_len_[_dep] > 0); \
    queue_head_[_dep] = pmod((queue_head_[_dep] + 1), ALGOR_QUEUE_SIZE); \
    --queue_len_[_dep]; \
} while(0)

/* ************************************************************************************** */
/* following are the procedures for obase with duality , always cutting based on shorter bar
 */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::shorter_duo_sim_obase_space_cut(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    shorter_duo_sim_obase_bicut(l_son->t0, l_son->t1, l_son->grid, f);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0)
                    shorter_duo_sim_obase_bicut(l_father->t0, l_father->t1, l_father->grid, f);
                else
                    cilk_spawn shorter_duo_sim_obase_bicut(l_father->t0, l_father->t1, l_father->grid, f);
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                const Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                const int l_padding = 2 * l_slope;
                const bool can_cut = cut_lb ? (lb >= 2 * thres && tb + l_padding > dx_recursive_[level]) : (tb >= 2 * thres && lb + l_padding > dx_recursive_[level]);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push 
                     * it into the circular queue 
                     */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut! */
                    if (cut_lb) {
                        const int mid = (lb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle triangular minizoid (gray) into 
                         * circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push the left big trapezoid (black)
                         * into circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push the right big trapezoid (black)
                         * into circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                    } /* end if (cut_lb) */
                    else {
                        /* cut_tb */
                        const int mid = (tb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);
                        const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);

                        /* push left black sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push right black sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = ul_start + mid;;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push the middle gray triangular minizoid into 
                         * circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = ul_start + mid;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end else (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

/* This is for boundary region space cut! , always cutting based on the shorter bar
 */
template <int N_RANK> template <typename F, typename BF>
inline void Algorithm<N_RANK>::shorter_duo_sim_obase_space_cut_p(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f, BF const & bf)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    shorter_duo_sim_obase_bicut_p(l_son->t0, l_son->t1, l_son->grid, f, bf);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0) {
                    shorter_duo_sim_obase_bicut_p(l_father->t0, l_father->t1, l_father->grid, f, bf);
                } else {
                    cilk_spawn shorter_duo_sim_obase_bicut_p(l_father->t0, l_father->t1, l_father->grid, f, bf);
                }
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                const int l_padding = 2 * l_slope;
                const bool l_touch_boundary = touch_boundary(level, lt, l_father_grid);
                const bool can_cut = cut_lb ? (l_touch_boundary ? (lb >= 2 * thres && tb > dx_recursive_boundary_[level]) : (lb >= 2 * thres && tb + l_padding > dx_recursive_[level])) : (l_touch_boundary ? (lb == phys_length_[level] ? (tb >= 2 * thres && lb > dx_recursive_boundary_[level]) : (tb >= 2 * thres && lb > dx_recursive_boundary_[level])) : (tb >= 2 * thres && lb + l_padding > dx_recursive_[level]));
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push
                     * it into the circular queue
                    */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut */
                    if (cut_lb) {
                        /* if cutting lb, there's no initial cut! */
                        assert(lb != phys_length_[level] || l_father_grid.dx0[level] != 0 || l_father_grid.dx1[level] != 0);
                        const int mid = lb/2;
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle gray minizoid
                         * into circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end if (cut_lb) */
                    else { /* cut_tb */
                        if (lb == phys_length_[level] && l_father_grid.dx0[level] == 0 && l_father_grid.dx1[level] == 0) { /* initial cut on the dimension */
                            assert(l_father_grid.dx0[level] == 0);
                            assert(l_father_grid.dx1[level] == 0);
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* merge the big black trapezoids */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end + (ul_start - l_start) + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push middle minizoid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        } else { /* NOT the initial cut! */
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push one sub-grid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }                    
                    } /* end if (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

/* Always cut into the longer bar, and keep the longer bar within the cutting threshold */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::duo_sim_obase_space_cut(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    duo_sim_obase_bicut(l_son->t0, l_son->t1, l_son->grid, f);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0)
                    duo_sim_obase_bicut(l_father->t0, l_father->t1, l_father->grid, f);
                else
                    cilk_spawn duo_sim_obase_bicut(l_father->t0, l_father->t1, l_father->grid, f);
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                const Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = 2 * l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb >= tb);
                const int l_padding = 2 * l_slope;
                const bool can_cut = cut_lb ? (lb >= 2 * thres && lb + l_padding > dx_recursive_[level]) : (tb >= 2 * thres && tb + l_padding > dx_recursive_[level]);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push 
                     * it into the circular queue 
                     */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut! */
                    if (cut_lb) {
                        const int mid = (lb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push one sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push one sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = l_start + mid;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start + mid;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_start + mid;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                    } /* end if (cut_lb) */
                    else {
                        /* cut_tb */
                        const int thres = (lt * l_slope);
                        const int mid = (lb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push one sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end else (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

/* This is for boundary region space cut! */
/* Always cut into the longer bar, and keep the longer bar within the cutting threshold */
template <int N_RANK> template <typename F, typename BF>
inline void Algorithm<N_RANK>::duo_sim_obase_space_cut_p(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f, BF const & bf)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    duo_sim_obase_bicut_p(l_son->t0, l_son->t1, l_son->grid, f, bf);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0) {
                    duo_sim_obase_bicut_p(l_father->t0, l_father->t1, l_father->grid, f, bf);
                } else {
                    cilk_spawn duo_sim_obase_bicut_p(l_father->t0, l_father->t1, l_father->grid, f, bf);
                }
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = 2 * l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb >= tb);
                const bool l_touch_boundary = touch_boundary(level, lt, l_father_grid);
                const int l_padding = 2 * l_slope;
                const bool can_cut = cut_lb ? (l_touch_boundary ? (lb == phys_length_[level] ? (lb >= 2 * thres + l_padding && lb > dx_recursive_boundary_[level]) : (lb >= 2 * thres && lb > dx_recursive_boundary_[level])) : (lb >= 2 * thres && lb + l_padding > dx_recursive_[level])) : (l_touch_boundary ? (tb >= 2 * thres && tb > dx_recursive_boundary_[level]) : (tb >= 2 * thres && tb + l_padding > dx_recursive_[level]));
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push
                     * it into the circular queue
                    */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut */
                    if (cut_lb) {
                        if (lb == phys_length_[level]) { /* initial cut on the dimension */
                            const int mid = (int)lb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start + l_slope;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_start + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end - l_slope;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push one sub-grid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = l_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = l_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* initial cut - merge triangles! */
                            l_son_grid.x0[level] = l_end - l_slope;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = l_end + l_slope;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        } else { /* NOT the initial cut! */
                            const int mid = (int)lb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = l_start + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push one sub-grid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = l_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = l_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }
                    } /* end if (cut_lb) */
                    else { /* cut_tb */
                        /* if cutting tb, there's no initial cut! */
                        assert(tb != phys_length_[level]);
                        const int mid = (int)lb/2;
                        const int thres = (lt * l_slope);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push one sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end if (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}
/* following are the procedures for obase */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::sim_obase_space_cut(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    sim_obase_bicut(l_son->t0, l_son->t1, l_son->grid, f);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0)
                    sim_obase_bicut(l_father->t0, l_father->t1, l_father->grid, f);
                else
                    cilk_spawn sim_obase_bicut(l_father->t0, l_father->t1, l_father->grid, f);
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                const Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = 2 * l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const bool can_cut = (lb >= 2 * thres && lb > dx_recursive_[level]);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push 
                     * it into the circular queue 
                     */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut! */
                    const int mid = (int)lb/2;
                    Grid_Info<N_RANK> l_son_grid = l_father_grid;
                    const int l_start = (l_father_grid.x0[level]);
                    const int l_end = (l_father_grid.x1[level]);

                    /* push one sub-grid into circular queue of (curr_dep) */
                    l_son_grid.x0[level] = l_start;
                    l_son_grid.dx0[level] = l_slope;
                    l_son_grid.x1[level] = l_start + mid;
                    l_son_grid.dx1[level] = -l_slope;
                    assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                    /* push one sub-grid into circular queue of (curr_dep) */
                    l_son_grid.x0[level] = l_start + mid;
                    l_son_grid.dx0[level] = l_slope;
                    l_son_grid.x1[level] = l_end;
                    l_son_grid.dx1[level] = -l_slope;
                    assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                    /* cilk_sync */
                    const int next_dep_pointer = (curr_dep + 1) & 0x1;
                    /* push one sub-grid into circular queue of (curr_dep + 1)*/
                    l_son_grid.x0[level] = l_start + mid;
                    l_son_grid.dx0[level] = -l_slope;
                    l_son_grid.x1[level] = l_start + mid;
                    l_son_grid.dx1[level] = l_slope;
                    assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                    push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                    if (l_father_grid.dx0[level] != l_slope) {
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start;
                        l_son_grid.dx1[level] = l_slope;
                        assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    }
                    if (l_father_grid.dx1[level] != -l_slope) {
                        l_son_grid.x0[level] = l_end;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    }
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

/* This is for boundary region space cut! */
template <int N_RANK> template <typename F, typename BF>
inline void Algorithm<N_RANK>::sim_obase_space_cut_p(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f, BF const & bf)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    sim_obase_bicut_p(l_son->t0, l_son->t1, l_son->grid, f, bf);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0) {
                    sim_obase_bicut_p(l_father->t0, l_father->t1, l_father->grid, f, bf);
                } else {
                    cilk_spawn sim_obase_bicut_p(l_father->t0, l_father->t1, l_father->grid, f, bf);
                }
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = 2 * l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const bool l_touch_boundary = touch_boundary(level, lt, l_father_grid);
                const bool can_cut = l_touch_boundary ? (lb >= 2 * thres && lb > dx_recursive_boundary_[level]) : (lb >= 2 * thres && lb > dx_recursive_[level]);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push
                     * it into the circular queue
                    */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut */
                    const int mid = (int)lb/2;
                    Grid_Info<N_RANK> l_son_grid = l_father_grid;
                    const int l_start = (l_father_grid.x0[level]);
                    const int l_end = (l_father_grid.x1[level]);

                    /* push one sub-grid into circular queue of (curr_dep) */
                    l_son_grid.x0[level] = l_start;
                    l_son_grid.dx0[level] = l_slope;
                    l_son_grid.x1[level] = l_start + mid;
                    l_son_grid.dx1[level] = -l_slope;
                    assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                    /* push one sub-grid into circular queue of (curr_dep) */
                    l_son_grid.x0[level] = l_start + mid;
                    l_son_grid.dx0[level] = l_slope;
                    l_son_grid.x1[level] = l_end;
                    l_son_grid.dx1[level] = -l_slope;
                    assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                    /* cilk_sync */
                    const int next_dep_pointer = (curr_dep + 1) & 0x1;
                    /* push one sub-grid into circular queue of (curr_dep + 1)*/
                    l_son_grid.x0[level] = l_start + mid;
                    l_son_grid.dx0[level] = -l_slope;
                    l_son_grid.x1[level] = l_start + mid;
                    l_son_grid.dx1[level] = l_slope;
                    assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                    push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                    if (lb == phys_length_[level]) {
                        /* initial cut - merge triangles! */
                        l_son_grid.x0[level] = l_end;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_slope;
                        assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } else {
                        if (l_father_grid.dx0[level] != l_slope) {
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = l_start;
                            l_son_grid.dx1[level] = l_slope;
                            assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }
                        if (l_father_grid.dx1[level] != -l_slope) {
                            l_son_grid.x0[level] = l_end;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            assert(l_son_grid.x0[level] <= l_son_grid.x1[level]);
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }
                    }
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

/* This is the version for interior region cut! */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::shorter_duo_sim_obase_bicut(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false;
    Grid_Info<N_RANK> l_son_grid;
#if STAT
    int l_count_cut = 0;
    int l_bottom_total_area = 1;
    int l_top_total_area = 1;
    int l_total_points;
#endif

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        bool cut_lb = (lb < tb);
        thres = (slope_[i] * lt);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (cut_lb ? (lb >= 2 * thres && tb + l_padding > dx_recursive_[i]) : (tb >= 2 * thres && lb + l_padding > dx_recursive_[i]));
        /* as long as there's one dimension can conduct a cut, we conduct a 
         * multi-dimensional cut!
         */
#if STAT
        l_count_cut = (l_can_cut ? l_count_cut+1 : l_count_cut);
#endif
    }

#if STAT
//    l_total_points = l_bottom_total_area * t1 / 3 - l_top_total_area * t0 / 3;
#endif
    if (sim_can_cut) {
        /* cut into space */
#if STAT
    // sim_count_cut[l_count_cut] = (l_count_cut > 0 ? sim_count_cut[l_count_cut] + 1 : sim_count_cut[l_count_cut]);
        ++sim_count_cut[l_count_cut];
#endif
        shorter_duo_sim_obase_space_cut(t0, t1, grid, f);
        return;
    // } else if (lt > dt_recursive_ && l_total_points > Z) {
    } else if (lt > dt_recursive_) {
        /* cut into time */
//        assert(dt_recursive_ >= r_t);
        assert(lt > dt_recursive_);
        int halflt = lt / 2;
        /* cutting halflt align to unroll_ factor */
        halflt -= (halflt % lcm_unroll_);
#if DEBUG
        printf("halflt = %d\n", halflt);
#endif
        l_son_grid = grid;
        shorter_duo_sim_obase_bicut(t0, t0+halflt, l_son_grid, f);

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = grid.dx0[i];
            l_son_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = grid.dx1[i];
        }
        shorter_duo_sim_obase_bicut(t0+halflt, t1, l_son_grid, f);
        return;
    } else {
        // base case
#if DEBUG
        printf("call interior!\n");
        print_grid(stdout, t0, t1, grid);
        // fprintf(stderr, "l_total_points = %d\n", l_total_points);
#endif
#if STAT
        ++interior_region_count;
#endif
        f(t0, t1, grid);
//        base_case_kernel_interior(t0, t1, grid, f);
        return;
    }  
}

/* This is the version for interior region cut! */
/* Always cut into the longer bar, and keep the longer bar within the cutting threshold */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::duo_sim_obase_bicut(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false;
    Grid_Info<N_RANK> l_son_grid;
#if STAT
    int l_count_cut = 0;
    int l_bottom_total_area = 1;
    int l_top_total_area = 1;
    int l_total_points;
#endif

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        const bool cut_lb = (lb >= tb);
        const int l_padding = 2 * slope_[i];
        thres = (2 * slope_[i] * lt);
        sim_can_cut = sim_can_cut || (cut_lb ? (lb >= 2 * thres && lb + l_padding > dx_recursive_[i]) : (tb >= 2 * thres && tb + l_padding > dx_recursive_[i]));
        /* as long as there's one dimension can conduct a cut, we conduct a 
         * multi-dimensional cut!
         */
#if STAT
        l_count_cut = (l_can_cut ? l_count_cut+1 : l_count_cut);
#endif
    }

#if STAT
//    l_total_points = l_bottom_total_area * t1 / 3 - l_top_total_area * t0 / 3;
#endif
    if (sim_can_cut) {
        /* cut into space */
#if STAT
    // sim_count_cut[l_count_cut] = (l_count_cut > 0 ? sim_count_cut[l_count_cut] + 1 : sim_count_cut[l_count_cut]);
        ++sim_count_cut[l_count_cut];
#endif
        duo_sim_obase_space_cut(t0, t1, grid, f);
        return;
    // } else if (lt > dt_recursive_ && l_total_points > Z) {
    } else if (lt > dt_recursive_) {
        /* cut into time */
//        assert(dt_recursive_ >= r_t);
        assert(lt > dt_recursive_);
        int halflt = lt / 2;
        l_son_grid = grid;
        duo_sim_obase_bicut(t0, t0+halflt, l_son_grid, f);

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = grid.dx0[i];
            l_son_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = grid.dx1[i];
        }
        duo_sim_obase_bicut(t0+halflt, t1, l_son_grid, f);
        return;
    } else {
        // base case
#if DEBUG
        printf("call interior!\n");
        print_grid(stdout, t0, t1, grid);
        // fprintf(stderr, "l_total_points = %d\n", l_total_points);
#endif
#if STAT
        ++interior_region_count;
#endif
        f(t0, t1, grid);
//        base_case_kernel_interior(t0, t1, grid, f);
        return;
    }  
}

/* This is the version for boundary region cut! */
template <int N_RANK> template <typename F, typename BF>
inline void Algorithm<N_RANK>::shorter_duo_sim_obase_bicut_p(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f, BF const & bf)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false, call_boundary = false;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    int l_dt_stop;

#if STAT
    int l_count_cut = 0;
    int l_bottom_total_area = 1;
    int l_top_total_area = 1;
    int l_total_points;
#endif

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid);
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        thres = (slope_[i] * lt);
        /* l_father_grid may be mapped to a new region in touch_boundary() */
        /* for the initial cut, we exclude the begining and end point to minimize
         * the overhead on boundary
        */
        /* lb == phys_length_[i] indicates an initial cut! */
        bool cut_lb = (lb < tb);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (cut_lb ? (l_touch_boundary ? (lb >= 2 * thres && tb > dx_recursive_boundary_[i]) : (lb >= 2 * thres && tb + l_padding > dx_recursive_[i])) : (l_touch_boundary ? (lb == phys_length_[i] ? (tb >= 2 * thres && lb > dx_recursive_boundary_[i]) : (tb >= 2 * thres && lb > dx_recursive_boundary_[i])) : (tb > 2 * thres && lb + l_padding > dx_recursive_[i])));
        call_boundary |= l_touch_boundary;
#if STAT
        l_count_cut = (l_can_cut ? l_count_cut + 1 : l_count_cut);
        l_bottom_total_area *= lb;
        l_top_total_area *= tb;
#endif
    }

#if STAT
//    l_total_points = l_bottom_total_area * t1 / 3 - l_top_total_area * t0 / 3;
#endif

    if (sim_can_cut) {
        /* cut into space */
        /* push the first l_father_grid that can be cut into the circular queue */
        /* boundary cuts! */
#if STAT
    // sim_count_cut[l_count_cut] = (l_count_cut > 0 ? sim_count_cut[l_count_cut] + 1 : sim_count_cut[l_count_cut]);
        ++sim_count_cut[l_count_cut];
#endif
        if (call_boundary) 
            shorter_duo_sim_obase_space_cut_p(t0, t1, l_father_grid, f, bf);
        else
            shorter_duo_sim_obase_space_cut(t0, t1, l_father_grid, f);
        return;
    } 

    if (call_boundary)
        l_dt_stop = max(dt_recursive_boundary_, lcm_unroll_);
    else
        l_dt_stop = dt_recursive_;

    if (lt > l_dt_stop) {
        /* cut into time */
        int halflt = lt / 2;
        /* cut halflt align to unroll_ */
        halflt -= (halflt % lcm_unroll_);
#if DEBUG
        printf("halflt = %d\n", halflt);
#endif
        l_son_grid = l_father_grid;
        if (call_boundary) {
            shorter_duo_sim_obase_bicut_p(t0, t0+halflt, l_son_grid, f, bf);
        } else {
            shorter_duo_sim_obase_bicut(t0, t0+halflt, l_son_grid, f);
        }

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = l_father_grid.dx0[i];
            l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = l_father_grid.dx1[i];
        }
        if (call_boundary) {
            shorter_duo_sim_obase_bicut_p(t0+halflt, t1, l_son_grid, f, bf);
        } else {
            shorter_duo_sim_obase_bicut(t0+halflt, t1, l_son_grid, f);
        }
        return;
    } 

    // if (l_total_area <= Z || base_cube_t) {
        /* for base_cube_t: -- prevent too small time cut! 
         *      (cut_lb && lb > dx_recursive_boundary_ && lb < 2 * thres)
         *  ||  (!cut_lb && tb > dx_recursive_boundary_ && lb < thres)
         */
        // base case
#if DEBUG
        printf("call boundary!\n");
        print_grid(stdout, t0, t1, l_father_grid);
#endif
#if STAT
        ++boundary_region_count;
        boundary_points_count += l_total_points;
#endif
        if (call_boundary) {
            bf(t0, t1, l_father_grid);
//            base_case_kernel_boundary(t0, t1, l_father_grid, bf);
        } else {
            f(t0, t1, l_father_grid);
        }
        return;
}

/************************************************************************************
 * Adaptive region cutting algorithm for irregular stencil computation **************
 ************************************************************************************/
#define adaptive_space_can_cut(_dim) (cut_lb ? (lb >= 2 * thres && tb + l_padding > dx_recursive_[_dim]) : (tb >= 2 * thres && lb + l_padding > dx_recursive_[_dim]))

#define adaptive_space_can_cut_p(_dim) (cut_lb ? ((l_touch_boundary || cross_region) ? (lb >= 2 * thres && tb + l_padding > dx_recursive_boundary_[_dim]) : (lb >= 2 * thres && tb + l_padding > dx_recursive_[_dim])) : ((l_touch_boundary || cross_region) ? (lb == phys_length_[_dim] ? (tb >= 2 * thres && lb + l_padding > dx_recursive_boundary_[_dim]) : (tb >= 2 * thres && lb + l_padding > dx_recursive_boundary_[_dim])) : (tb >= 2 * thres && lb + l_padding > dx_recursive_[_dim])))

template <int N_RANK> 
inline void Algorithm<N_RANK>::adaptive_space_bicut(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    adaptive_bicut(l_son->t0, l_son->t1, l_son->grid, region_n);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0)
                    adaptive_bicut(l_father->t0, l_father->t1, l_father->grid, region_n);
                else
                    cilk_spawn adaptive_bicut(l_father->t0, l_father->t1, l_father->grid, region_n);
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                const Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                const int l_padding = 2 * l_slope;
                const bool can_cut = adaptive_space_can_cut(level);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push 
                     * it into the circular queue 
                     */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut! */
                    if (cut_lb) {
                        const int mid = (lb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle triangular minizoid (gray) into 
                         * circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push the left big trapezoid (black)
                         * into circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push the right big trapezoid (black)
                         * into circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                    } /* end if (cut_lb) */
                    else {
                        /* cut_tb */
                        const int mid = (tb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);
                        const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);

                        /* push left black sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push right black sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = ul_start + mid;;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push the middle gray triangular minizoid into 
                         * circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = ul_start + mid;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end else (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

template <int N_RANK> 
inline void Algorithm<N_RANK>::adaptive_space_bicut_p(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    adaptive_bicut_p(l_son->t0, l_son->t1, l_son->grid);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0) {
                    adaptive_bicut_p(l_father->t0, l_father->t1, l_father->grid);
                } else {
                    cilk_spawn adaptive_bicut_p(l_father->t0, l_father->t1, l_father->grid);
                }
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                const int l_padding = 2 * l_slope;
                const bool cross_region = (region_n < 0);
                const bool l_touch_boundary = touch_boundary(level, lt, l_father_grid);
                const bool can_cut = adaptive_space_can_cut_p(level);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push
                     * it into the circular queue
                    */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut */
                    if (cut_lb) {
                        /* if cutting lb, there's no initial cut! */
                        assert(lb != phys_length_[level] || l_father_grid.dx0[level] != 0 || l_father_grid.dx1[level] != 0);
                        const int mid = lb/2;
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle gray minizoid
                         * into circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end if (cut_lb) */
                    else { /* cut_tb */
                        if (lb == phys_length_[level] && l_father_grid.dx0[level] == 0 && l_father_grid.dx1[level] == 0) { /* initial cut on the dimension */
                            assert(l_father_grid.dx0[level] == 0);
                            assert(l_father_grid.dx1[level] == 0);
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* merge the big black trapezoids */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end + (ul_start - l_start) + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push middle minizoid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        } else { /* NOT the initial cut! */
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push one sub-grid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }                    
                    } /* end if (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

/* This is the version for interior and pure region cut! */
template <int N_RANK> 
inline void Algorithm<N_RANK>::adaptive_bicut(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false;
    int l_unroll = (*opks_[region_n]).unroll_;
    /* because currently we only check the corner points of the trapezoids,
     * so we can NOT rely on the region_n tranferred in, e.g. for leap-frog
     * style stencil, we employ different computing kernel for even / odd
     * time steps, it's likely that for bigger trapezoids, all corner points
     * denotes a pure region, but its sub-region doesn't
     */

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        bool cut_lb = (lb < tb);
        thres = (slope_[i] * lt);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (adaptive_space_can_cut(i));
    }

    if (sim_can_cut) {
        adaptive_space_bicut(t0, t1, grid, region_n);
        return;
    } else if (lt > dt_recursive_) {
        /* cut into time */
        assert(lt > dt_recursive_);
        int halflt = lt / 2;
        /* cutting halflt align to unroll_ factor */
        halflt -= (halflt > l_unroll ? (halflt % l_unroll) : 0);
        Grid_Info<N_RANK> l_son_grid = grid;
        adaptive_bicut(t0, t0+halflt, l_son_grid, region_n);

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = grid.dx0[i];
            l_son_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = grid.dx1[i];
        }
        adaptive_bicut(t0+halflt, t1, l_son_grid, region_n);
        return;
    } else {
        // base case
        if (t1 - t0 < l_unroll) {
#if DEBUG
        printf("call cond_kernel_ <%d>, l_unroll = %d!\n", region_n, l_unroll);
        print_grid(stdout, t0, t1, grid);
#endif
            (*opks_[region_n]).cond_kernel_[0](t0, t1, grid);
        } else {
#if DEBUG
        printf("call kernel_ <%d>, l_unroll = %d!\n", region_n, l_unroll);
        print_grid(stdout, t0, t1, grid);
#endif
            (*opks_[region_n]).kernel_[0](t0, t1, grid);
        }
        return;
    }  
}

/* This is the version for boundary region cut! */
template <int N_RANK> 
inline void Algorithm<N_RANK>::adaptive_bicut_p(int t0, int t1, Grid_Info<N_RANK> const grid)
{
    /* the F, BF are generic interior and boundary function generated by
     * Pochoir compiler, respectively
     */
    const int lt = t1 - t0;
    bool sim_can_cut = false, call_boundary = false;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    int l_dt_stop, l_unroll;
    // const int region_n = (*pure_region_)(t0, t1, l_father_grid);;
    const int region_n = (*color_region_)(t0, t1, l_father_grid);;
    const bool cross_region = (region_n == CROSS_REGION);

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid);
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        thres = (slope_[i] * lt);
        bool cut_lb = (lb < tb);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (adaptive_space_can_cut_p(i));
        call_boundary |= l_touch_boundary;
    }

    if (sim_can_cut) {
        if (call_boundary || cross_region) 
            adaptive_space_bicut_p(t0, t1, l_father_grid, region_n);
        else
            adaptive_space_bicut(t0, t1, l_father_grid, region_n);
        return;
    } 

    if (call_boundary || cross_region) {
        l_dt_stop = dt_recursive_boundary_;
        l_unroll = lcm_unroll_;
    } else {
        assert (region_n >= 0);
        l_dt_stop = dt_recursive_;
        l_unroll = (*opks_[region_n]).unroll_;
    }

    if (lt > l_dt_stop) {
        /* cut into time */
        int halflt = lt / 2;
        /* cut halflt align to unroll_ 
         * for mixed region: we align it to lcm_unroll_;
         * for pure region: we refine it to pgk_[region_n].unroll_;
         */
        halflt -= (halflt > l_unroll ? (halflt % l_unroll) : 0);
        l_son_grid = l_father_grid;
        if (call_boundary || cross_region) {
            adaptive_bicut_p(t0, t0+halflt, l_son_grid);
        } else {
            adaptive_bicut(t0, t0+halflt, l_son_grid, region_n);
        }

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = l_father_grid.dx0[i];
            l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = l_father_grid.dx1[i];
        }
        if (call_boundary || cross_region) {
            adaptive_bicut_p(t0+halflt, t1, l_son_grid);
        } else {
            adaptive_bicut(t0+halflt, t1, l_son_grid, region_n);
        }
        return;
    } 

    /* Serial Space Cut: on spatial dimension 'i' */
    if (cross_region) {
#if DEBUG
        printf("Serial Space Cut called !!!\n");
        print_grid(stderr, t0, t1, l_father_grid);
#endif
        /* keep cutting until every point is in a pure sub-region */
        assert(t1 - t0 == 1);
        for (int i = 0; i < N_RANK; ++i) {
            const int len_i = l_father_grid.x1[i] - l_father_grid.x0[i];
            if (len_i > 1) {
                int mid_i = l_father_grid.x0[i] + len_i / 2;
                l_son_grid = l_father_grid;
                // l_son_grid.x0[i] = l_father_grid.x0[i];
                l_son_grid.x1[i] = mid_i;
                /* due to that t1-t0 == 1, we can probably parallelize
                 * the subzoids generated by serial space cut here!?
                 */
                adaptive_bicut_p(t0, t1, l_son_grid);
                l_son_grid.x0[i] = mid_i;
                l_son_grid.x1[i] = l_father_grid.x1[i];
                adaptive_bicut_p(t0, t1, l_son_grid);
                return;
            }
        }
    }
    // base case
    assert(region_n >= 0);

    if (call_boundary) {
        /* boundary region */
        // if (t1 - t0 < opks_[region_n].unroll_) {
        if (t1 - t0 < l_unroll) {
#if DEBUG
        printf("call cond_boundary_kernel <%d>, l_unroll = %d!\n", region_n, l_unroll);
        print_grid(stdout, t0, t1, l_father_grid);
#endif
            (*opks_[region_n]).cond_bkernel_[0](t0, t1, l_father_grid);
        } else {
#if DEBUG
        printf("call boundary_kernel <%d>, l_unroll = %d!\n", region_n, l_unroll);
        print_grid(stdout, t0, t1, l_father_grid);
#endif
            (*opks_[region_n]).bkernel_[0](t0, t1, l_father_grid);
        }
    } else {
        // if (t1 - t0 < opks_[region_n].unroll_) {
        if (t1 - t0 < l_unroll) {
#if DEBUG
        printf("call cond_kernel_ <%d>, l_unroll = %d!\n", region_n, l_unroll);
        print_grid(stdout, t0, t1, l_father_grid);
#endif
            (*opks_[region_n]).cond_kernel_[0](t0, t1, l_father_grid);
        } else {
#if DEBUG
        printf("call kernel_ <%d>, l_unroll = %d!\n", region_n, l_unroll);
        print_grid(stdout, t0, t1, l_father_grid);
#endif
            (*opks_[region_n]).kernel_[0](t0, t1, l_father_grid);
        }
    }
    return;
}

/************************************************************************************
 * Generating Plans of Adaptive region cutting algorithm for irregular stencil      * 
 * computation                                                                      *
 ************************************************************************************/
// #define gen_plan_space_can_cut(_dim) (cut_lb ? (lb >= 2 * thres && tb + l_padding > dx_recursive_[_dim]) : (tb >= 2 * thres && lb + l_padding > dx_recursive_[_dim]))

// The version doesn't count the boundary region
// #define gen_plan_space_can_cut_p(_dim) (cut_lb ? (lb >= 2 * thres && tb > dx_homo_[_dim]) : (tb >= 2 * thres && lb > dx_homo_[_dim]))

// The version consider minimizing the boundary region
#define gen_plan_space_can_cut_p(_dim) (l_touch_boundary ? (cut_lb ? (lb >= 2 * thres && tb > dx_recursive_boundary_[_dim]) : (tb >= 2 * thres && lb > dx_recursive_boundary_[_dim])) : (cut_lb ? (lb >= 2 * thres && tb > dx_homo_[_dim]) : (tb >= 2 * thres && lb > dx_homo_[_dim])))

template <int N_RANK> 
inline void Algorithm<N_RANK>::gen_plan_space_bicut_p(Node_Info<N_RANK> * parent, int t0, int t1, Grid_Info<N_RANK> const grid, int rec_level)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    gen_plan_bicut_p(parent, l_son->t0, l_son->t1, l_son->grid, rec_level+1);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                gen_plan_bicut_p(parent, l_father->t0, l_father->t1, l_father->grid, rec_level+1);
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                // const int l_padding = 2 * l_slope;
                const bool l_touch_boundary = touch_boundary(level, lt, l_father_grid);
                const bool can_cut = gen_plan_space_can_cut_p(level);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push
                     * it into the circular queue
                    */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut */
                    if (cut_lb) {
                        /* if cutting lb, there's no initial cut! */
                        assert(lb != phys_length_[level] || l_father_grid.dx0[level] != 0 || l_father_grid.dx1[level] != 0);
                        const int mid = lb/2;
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle gray minizoid
                         * into circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end if (cut_lb) */
                    else { /* cut_tb */
                        if (lb == phys_length_[level] && l_father_grid.dx0[level] == 0 && l_father_grid.dx1[level] == 0) { /* initial cut on the dimension */
                            assert(l_father_grid.dx0[level] == 0);
                            assert(l_father_grid.dx1[level] == 0);
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* merge the big black trapezoids */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end + (ul_start - l_start) + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push middle minizoid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        } else { /* NOT the initial cut! */
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push one sub-grid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }                    
                    } /* end if (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
        Node_Info<N_RANK> * l_node = new Node_Info<N_RANK>(IS_SYNC);
        (*tree_).add_node(parent, l_node, IS_SYNC);
        ++sz_sync_data_;
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
    return;
}

template <int N_RANK> 
inline void Algorithm<N_RANK>::gen_plan_space_cut_p(Node_Info<N_RANK> * parent, int t0, int t1, Grid_Info<N_RANK> const grid, int rec_level)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    gen_plan_cut_p(parent, l_son->t0, l_son->t1, l_son->grid, rec_level+1);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                gen_plan_cut_p(parent, l_father->t0, l_father->t1, l_father->grid, rec_level+1);
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                // const int l_padding = 2 * l_slope;
                const bool l_touch_boundary = touch_boundary(level, lt, l_father_grid);
                const bool can_cut = gen_plan_space_can_cut_p(level);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push
                     * it into the circular queue
                    */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut */
                    if (cut_lb) {
                        /* if cutting lb, there's no initial cut! */
                        assert(lb != phys_length_[level] || l_father_grid.dx0[level] != 0 || l_father_grid.dx1[level] != 0);
                        const int mid = lb/2;
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle gray minizoid
                         * into circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end if (cut_lb) */
                    else { /* cut_tb */
                        if (lb == phys_length_[level] && l_father_grid.dx0[level] == 0 && l_father_grid.dx1[level] == 0) { /* initial cut on the dimension */
                            assert(l_father_grid.dx0[level] == 0);
                            assert(l_father_grid.dx1[level] == 0);
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* merge the big black trapezoids */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end + (ul_start - l_start) + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push middle minizoid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        } else { /* NOT the initial cut! */
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push one sub-grid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }                    
                    } /* end if (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
        Node_Info<N_RANK> * l_node = new Node_Info<N_RANK>(IS_SYNC);
        (*tree_).add_node(parent, l_node, IS_SYNC);
        ++sz_sync_data_;
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
    return;
}


template <int N_RANK> 
inline void Algorithm<N_RANK>::gen_plan_bicut_p(Node_Info<N_RANK> * parent, int t0, int t1, Grid_Info<N_RANK> const grid, int rec_level)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false, call_boundary = false;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    int l_dt_stop;
    const unsigned int l_unroll = lcm_unroll_;
    // int region_n = (*pure_region_)(t0, t1, l_father_grid);
    Homogeneity homo = (*color_region_)(t0-time_shift_, t1-time_shift_, l_father_grid);
    // const bool cross_region = (region_n == CROSS_REGION);
    const bool cross_region = !(homo.is_homogeneous());
    Node_Info<N_RANK> * l_internal = new Node_Info<N_RANK>(t0, t1, l_father_grid, homo);

    if (cross_region) {
        (*tree_).add_node(parent, l_internal, IS_INTERNAL);
    } else {
        (*homogeneity_vector_).push_back_unique(homo, rec_level);
        int region_n = (*homogeneity_vector_).get_index(homo);
        (*tree_).add_node(parent, l_internal, IS_SPAWN, homo, region_n);
        ++sz_base_data_;
        return;
    } 

    /* cross_region! */
    assert(cross_region);
    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid);
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        thres = (slope_[i] * lt);
        bool cut_lb = (lb < tb);
        // const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (gen_plan_space_can_cut_p(i));
        call_boundary |= l_touch_boundary;
    }

    if (sim_can_cut) {
        gen_plan_space_bicut_p(l_internal, t0, t1, l_father_grid, rec_level);
        return;
    } 

    l_dt_stop = dt_homo_;
    if (lt > l_dt_stop && lt > l_unroll) {
        /* cut into time */
        const unsigned int halflt = lt / 2;
        /* cut halflt align to unroll_ 
         * for mixed region: we align it to lcm_unroll_;
         * for pure region: we refine it to pgk_[region_n].unroll_;
         */
        l_son_grid = l_father_grid;
        gen_plan_bicut_p(l_internal, t0, t0+halflt, l_son_grid, rec_level+1);

        Node_Info<N_RANK> * l_node = new Node_Info<N_RANK>(IS_SYNC);
        (*tree_).add_node(l_internal, l_node, IS_SYNC);
        ++sz_sync_data_;

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = l_father_grid.dx0[i];
            l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = l_father_grid.dx1[i];
        }
        gen_plan_bicut_p(l_internal, t0+halflt, t1, l_son_grid, rec_level+1);
        return;
    } 

    /* Add Inhomogeneous node into the tree */
    (*homogeneity_vector_).push_back_unique(homo, rec_level);
    int region_n = (*homogeneity_vector_).get_index(homo);
    Node_Info<N_RANK> * l_leaf = new Node_Info<N_RANK>(t0, t1, l_father_grid, homo);
    (*tree_).add_node(l_internal, l_leaf, IS_SPAWN, homo, region_n);
    ++sz_base_data_;
    return;
}

template <int N_RANK> 
inline void Algorithm<N_RANK>::gen_plan_cut_p(Node_Info<N_RANK> * parent, int t0, int t1, Grid_Info<N_RANK> const grid, int rec_level)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false, call_boundary = false;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    int l_dt_stop;
    const int l_unroll = lcm_unroll_;
    // int region_n = (*pure_region_)(t0, t1, l_father_grid);
    Homogeneity homo = (*color_region_)(t0-time_shift_, t1-time_shift_, l_father_grid);
    // const bool cross_region = (region_n == CROSS_REGION);
    const bool cross_region = !(homo.is_homogeneous());
    Node_Info<N_RANK> * l_internal = new Node_Info<N_RANK>(t0, t1, l_father_grid, homo);

    if (cross_region) {
        (*tree_).add_node(parent, l_internal, IS_INTERNAL);
    } else {
        (*homogeneity_vector_).push_back_unique(homo, rec_level);
        int region_n = (*homogeneity_vector_).get_index(homo);
        (*tree_).add_node(parent, l_internal, IS_SPAWN, homo, region_n);
        ++sz_base_data_;
        return;
    } 

    /* cross_region! */
    assert(cross_region);
    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid);
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        thres = (slope_[i] * lt);
        bool cut_lb = (lb < tb);
        // const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (gen_plan_space_can_cut_p(i));
        call_boundary |= l_touch_boundary;
    }

    if (sim_can_cut) {
        gen_plan_space_cut_p(l_internal, t0, t1, l_father_grid, rec_level);
        return;
    } 

    l_dt_stop = dt_homo_;
    if (lt > l_dt_stop && lt > l_unroll) {
        /* cut into time */
        const unsigned int m = log2_floor((unsigned int)lt/l_unroll);
        const unsigned int halflt = l_unroll * pow2(m);
        /* cut halflt align to unroll_ 
         * for mixed region: we align it to lcm_unroll_;
         * for pure region: we refine it to pgk_[region_n].unroll_;
         */
        l_son_grid = l_father_grid;
        gen_plan_bicut_p(l_internal, t0, t0+halflt, l_son_grid, rec_level+1);

        Node_Info<N_RANK> * l_node = new Node_Info<N_RANK>(IS_SYNC);
        (*tree_).add_node(l_internal, l_node, IS_SYNC);
        ++sz_sync_data_;

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = l_father_grid.dx0[i];
            l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = l_father_grid.dx1[i];
        }
        gen_plan_cut_p(l_internal, t0+halflt, t1, l_son_grid, rec_level+1);
        return;
    } 

    /* Add Inhomogeneous node into the tree */
    (*homogeneity_vector_).push_back_unique(homo, rec_level);
    int region_n = (*homogeneity_vector_).get_index(homo);
    Node_Info<N_RANK> * l_leaf = new Node_Info<N_RANK>(t0, t1, l_father_grid, homo);
    (*tree_).add_node(l_internal, l_leaf, IS_SPAWN, homo, region_n);
    ++sz_base_data_;
    return;
}

/************************************************************************************
 * Generating Plans of Adaptive region cutting algorithm for irregular stencil      *
 * computation                                                                      *
 ************************************************************************************/

/************************************************************************************
 * Run the Plan for irregular stencil computation                                   *
 ************************************************************************************/
#define plan_space_can_cut(_dim) (cut_lb ? (lb >= 2 * thres && tb + l_padding > dx_recursive_[_dim]) : (tb >= 2 * thres && lb + l_padding > dx_recursive_[_dim]))

#define plan_space_can_cut_p(_dim) (cut_lb ? ((l_touch_boundary) ? (lb >= 2 * thres && tb > dx_recursive_boundary_[_dim]) : (lb >= 2 * thres && tb > dx_recursive_[_dim])) : ((l_touch_boundary) ? (tb >= 2 * thres && lb > dx_recursive_boundary_[_dim]) : (tb >= 2 * thres && lb + l_padding > dx_recursive_[_dim])))

template <int N_RANK> 
inline void Algorithm<N_RANK>::plan_space_bicut(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    plan_bicut(l_son->t0, l_son->t1, l_son->grid, region_n);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0)
                    plan_bicut(l_father->t0, l_father->t1, l_father->grid, region_n);
                else
                    cilk_spawn plan_bicut(l_father->t0, l_father->t1, l_father->grid, region_n);
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                const Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                const int l_padding = 2 * l_slope;
                const bool can_cut = plan_space_can_cut(level);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push 
                     * it into the circular queue 
                     */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut! */
                    if (cut_lb) {
                        const int mid = (lb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle triangular minizoid (gray) into 
                         * circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push the left big trapezoid (black)
                         * into circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push the right big trapezoid (black)
                         * into circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                    } /* end if (cut_lb) */
                    else {
                        /* cut_tb */
                        const int mid = (tb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);
                        const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);

                        /* push left black sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push right black sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = ul_start + mid;;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push the middle gray triangular minizoid into 
                         * circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = ul_start + mid;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end else (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

template <int N_RANK> 
inline void Algorithm<N_RANK>::plan_space_bicut_p(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    plan_bicut_p(l_son->t0, l_son->t1, l_son->grid, region_n);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0) {
                    plan_bicut_p(l_father->t0, l_father->t1, l_father->grid, region_n);
                } else {
                    cilk_spawn plan_bicut_p(l_father->t0, l_father->t1, l_father->grid, region_n);
                }
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                const int l_padding = 2 * l_slope;
                //const bool cross_region = (region_n < 0);
                const bool l_touch_boundary = touch_boundary(level, lt, l_father_grid);
                const bool can_cut = plan_space_can_cut_p(level);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push
                     * it into the circular queue
                    */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut */
                    if (cut_lb) {
                        /* if cutting lb, there's no initial cut! */
                        assert(lb != phys_length_[level] || l_father_grid.dx0[level] != 0 || l_father_grid.dx1[level] != 0);
                        const int mid = lb/2;
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle gray minizoid
                         * into circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end if (cut_lb) */
                    else { /* cut_tb */
                        if (lb == phys_length_[level] && l_father_grid.dx0[level] == 0 && l_father_grid.dx1[level] == 0) { /* initial cut on the dimension */
                            assert(l_father_grid.dx0[level] == 0);
                            assert(l_father_grid.dx1[level] == 0);
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* merge the big black trapezoids */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end + (ul_start - l_start) + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push middle minizoid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        } else { /* NOT the initial cut! */
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push one sub-grid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }                    
                    } /* end if (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

template <int N_RANK> 
inline void Algorithm<N_RANK>::plan_space_cut(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    plan_cut(l_son->t0, l_son->t1, l_son->grid, region_n);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0)
                    plan_cut(l_father->t0, l_father->t1, l_father->grid, region_n);
                else
                    cilk_spawn plan_cut(l_father->t0, l_father->t1, l_father->grid, region_n);
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                const Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                const int l_padding = 2 * l_slope;
                const bool can_cut = plan_space_can_cut(level);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push 
                     * it into the circular queue 
                     */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut! */
                    if (cut_lb) {
                        const int mid = (lb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle triangular minizoid (gray) into 
                         * circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push the left big trapezoid (black)
                         * into circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push the right big trapezoid (black)
                         * into circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                    } /* end if (cut_lb) */
                    else {
                        /* cut_tb */
                        const int mid = (tb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);
                        const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);

                        /* push left black sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push right black sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = ul_start + mid;;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push the middle gray triangular minizoid into 
                         * circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = ul_start + mid;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end else (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

template <int N_RANK> 
inline void Algorithm<N_RANK>::plan_space_cut_p(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    plan_cut_p(l_son->t0, l_son->t1, l_son->grid, region_n);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0) {
                    plan_cut_p(l_father->t0, l_father->t1, l_father->grid, region_n);
                } else {
                    cilk_spawn plan_cut_p(l_father->t0, l_father->t1, l_father->grid, region_n);
                }
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                const int l_padding = 2 * l_slope;
                //const bool cross_region = (region_n < 0);
                const bool l_touch_boundary = touch_boundary(level, lt, l_father_grid);
                const bool can_cut = plan_space_can_cut_p(level);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push
                     * it into the circular queue
                    */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut */
                    if (cut_lb) {
                        /* if cutting lb, there's no initial cut! */
                        assert(lb != phys_length_[level] || l_father_grid.dx0[level] != 0 || l_father_grid.dx1[level] != 0);
                        const int mid = lb/2;
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle gray minizoid
                         * into circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end if (cut_lb) */
                    else { /* cut_tb */
                        if (lb == phys_length_[level] && l_father_grid.dx0[level] == 0 && l_father_grid.dx1[level] == 0) { /* initial cut on the dimension */
                            assert(l_father_grid.dx0[level] == 0);
                            assert(l_father_grid.dx1[level] == 0);
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* merge the big black trapezoids */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end + (ul_start - l_start) + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push middle minizoid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        } else { /* NOT the initial cut! */
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push one sub-grid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }                    
                    } /* end if (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

#undef DEBUG_CUT_T_ALIGNMENT 
/* This is the version for interior region cut! */
template <int N_RANK> 
inline void Algorithm<N_RANK>::plan_bicut(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false;
    const int l_unroll = (*opks_[region_n]).unroll_;

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        bool cut_lb = (lb < tb);
        thres = (slope_[i] * lt);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (plan_space_can_cut(i));
    }

    if (sim_can_cut) {
        plan_space_bicut(t0, t1, grid, region_n);
        return;
    } else if (lt > dt_recursive_ && lt > l_unroll) {
        /* cut into time */
        assert (lt & 0x1 == 0);
        int halflt = lt / 2;
        /* cutting halflt align to unroll_ factor */
        // halflt -= (halflt > l_unroll ? (halflt % l_unroll) : 0);
        Grid_Info<N_RANK> l_son_grid = grid;
        plan_bicut(t0, t0+halflt, l_son_grid, region_n);

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = grid.dx0[i];
            l_son_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = grid.dx1[i];
        }
        plan_bicut(t0+halflt, t1, l_son_grid, region_n);
        return;
    } else {
        // base case
        assert ((t1 - time_shift_) % l_unroll == 0 && (t0 - time_shift_) % l_unroll == 0);
#ifdef DEBUG_CUT_T_ALIGNMENT 
        printf("<plan_bicut> kernel_[%d] l_unroll = %d, lt(%d, %d)\n", 
                region_n, l_unroll, t0, t1);
        // print_grid(stdout, t0, t1, grid);
        ++num_kernel_;
#endif
        (*opks_[region_n]).kernel_[0](t0, t1, grid);
        return;
    }  
}

/* This is the version for boundary region cut! */
template <int N_RANK> 
inline void Algorithm<N_RANK>::plan_bicut_p(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false, call_boundary = false;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    int l_dt_stop;
    const int l_unroll = (*opks_[region_n]).unroll_;

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid);
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        thres = (slope_[i] * lt);
        bool cut_lb = (lb < tb);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (plan_space_can_cut_p(i));
        call_boundary |= l_touch_boundary;
    }

    if (sim_can_cut) {
        if (call_boundary) 
            plan_space_bicut_p(t0, t1, l_father_grid, region_n);
        else
            plan_space_bicut(t0, t1, l_father_grid, region_n);
        return;
    } 

    if (call_boundary) {
        l_dt_stop = dt_recursive_boundary_;
    } else {
        assert (region_n >= 0);
        l_dt_stop = dt_recursive_;
    }

    if (lt > l_dt_stop && lt > l_unroll) {
        /* cut into time */
        assert(lt & 0x1 == 0);
        int halflt = lt / 2;
        /* cut halflt align to unroll_ 
         * for mixed region: we align it to lcm_unroll_;
         * for pure region: we refine it to pgk_[region_n].unroll_;
         */
        // halflt -= (halflt > l_unroll ? (halflt % l_unroll) : 0);
        l_son_grid = l_father_grid;
        if (call_boundary) {
            plan_bicut_p(t0, t0+halflt, l_son_grid, region_n);
        } else {
            plan_bicut(t0, t0+halflt, l_son_grid, region_n);
        }

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = l_father_grid.dx0[i];
            l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = l_father_grid.dx1[i];
        }
        if (call_boundary) {
            plan_bicut_p(t0+halflt, t1, l_son_grid, region_n);
        } else {
            plan_bicut(t0+halflt, t1, l_son_grid, region_n);
        }
        return;
    } 

    // base case
    assert(region_n >= 0);
    assert((t1 - time_shift_) % l_unroll == 0 && (t0 - time_shift_) % l_unroll == 0);

    if (call_boundary) {
#ifdef DEBUG_CUT_T_ALIGNMENT 
        printf("<plan_bicut_p> bkernel_[%d], l_unroll = %d, lt(%d, %d)\n", 
                region_n, l_unroll, t0, t1);
        // print_grid(stdout, t0, t1, l_father_grid);
        ++num_bkernel_;
#endif
        (*opks_[region_n]).bkernel_[0](t0, t1, l_father_grid);
    } else {
#ifdef DEBUG_CUT_T_ALIGNMENT 
        printf("<plan_bicut_p> kernel_[%d] l_unroll = %d, lt(%d, %d)\n", 
               region_n, l_unroll, t0, t1);
        // print_grid(stdout, t0, t1, l_father_grid);
        ++num_kernel_;
#endif
        (*opks_[region_n]).kernel_[0](t0, t1, l_father_grid);
    }
    return;
}

/* This is the version for interior region cut! */
template <int N_RANK> 
inline void Algorithm<N_RANK>::plan_cut(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false;
    const int l_unroll = (*opks_[region_n]).unroll_;

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        bool cut_lb = (lb < tb);
        thres = (slope_[i] * lt);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (plan_space_can_cut(i));
    }

    if (sim_can_cut) {
        plan_space_cut(t0, t1, grid, region_n);
        return;
    } else if (lt > dt_recursive_ && lt > l_unroll) {
        /* cut into time */
        const unsigned int m = log2_floor((unsigned int)lt/l_unroll);
        const unsigned int halflt = l_unroll * pow2(m);
        /* now the bottom half can be thrown to bicut procedure,
         * which involves no log2 / pow2 overhead
         */
        Grid_Info<N_RANK> l_son_grid = grid;
        plan_bicut(t0, t0+halflt, l_son_grid, region_n);

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = grid.dx0[i];
            l_son_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = grid.dx1[i];
        }
        plan_cut(t0+halflt, t1, l_son_grid, region_n);
        return;
    } else {
        // base case
        if ((t1 - time_shift_) % l_unroll != 0 || (t0 - time_shift_) % l_unroll != 0) {
        // if (t1 % l_unroll != 0 || t0 % l_unroll != 0) {
#ifdef DEBUG_CUT_T_ALIGNMENT 
            printf("<plan_cut> cond_kernel_[%d] l_unroll = %d, lt(%d, %d)\n", 
                    region_n, l_unroll, t0, t1);
            // print_grid(stdout, t0, t1, grid);
            ++num_cond_kernel_;
#endif
            (*opks_[region_n]).cond_kernel_[0](t0, t1, grid);
        } else {
            assert((t1 - time_shift_) % l_unroll == 0 && (t0 - time_shift_) % l_unroll == 0); 
#ifdef DEBUG_CUT_T_ALIGNMENT 
            printf("<plan_cut> kernel_[%d] l_unroll = %d, lt(%d, %d)\n", 
                    region_n, l_unroll, t0, t1);
            // print_grid(stdout, t0, t1, grid);
            ++num_kernel_;
#endif
            (*opks_[region_n]).kernel_[0](t0, t1, grid);
        }
        return;
    }  
}

/* This is the version for boundary region cut! */
template <int N_RANK> 
inline void Algorithm<N_RANK>::plan_cut_p(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false, call_boundary = false;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    int l_dt_stop;
    const int l_unroll = (*opks_[region_n]).unroll_;

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid);
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        thres = (slope_[i] * lt);
        bool cut_lb = (lb < tb);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (plan_space_can_cut_p(i));
        call_boundary |= l_touch_boundary;
    }

    if (sim_can_cut) {
        if (call_boundary) 
            plan_space_cut_p(t0, t1, l_father_grid, region_n);
        else
            plan_space_cut(t0, t1, l_father_grid, region_n);
        return;
    } 

    if (call_boundary) {
        l_dt_stop = dt_recursive_boundary_;
    } else {
        assert (region_n >= 0);
        l_dt_stop = dt_recursive_;
    }

    if (lt > l_dt_stop && lt > l_unroll) {
        /* cut into time */
        const unsigned int m = log2_floor((unsigned int)lt/l_unroll);
        const unsigned int halflt = l_unroll * pow2(m);
        l_son_grid = l_father_grid;
        if (call_boundary) {
            plan_bicut_p(t0, t0+halflt, l_son_grid, region_n);
        } else {
            plan_bicut(t0, t0+halflt, l_son_grid, region_n);
        }

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = l_father_grid.dx0[i];
            l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = l_father_grid.dx1[i];
        }
        if (call_boundary) {
            plan_cut_p(t0+halflt, t1, l_son_grid, region_n);
        } else {
            plan_cut(t0+halflt, t1, l_son_grid, region_n);
        }
        return;
    } 

    // base case
    assert(region_n >= 0);

    if (call_boundary) {
        if ((t1 - time_shift_) % l_unroll != 0 || (t0 - time_shift_) % l_unroll != 0) {
        // if (t1 % l_unroll != 0 || t0 % l_unroll != 0) {
#ifdef DEBUG_CUT_T_ALIGNMENT 
            printf("<plan_cut_p> cond_bkernel_[%d], l_unroll = %d, lt(%d, %d)\n", 
                    region_n, l_unroll, t0, t1);
            // print_grid(stdout, t0, t1, l_father_grid);
            ++num_cond_bkernel_;
#endif
            (*opks_[region_n]).cond_bkernel_[0](t0, t1, l_father_grid);
        } else {
            assert((t1 - time_shift_) % l_unroll == 0 && (t0 - time_shift_) % l_unroll == 0); 
#ifdef DEBUG_CUT_T_ALIGNMENT 
            printf("<plan_cut_p> bkernel_[%d] l_unroll = %d, lt(%d, %d)\n", 
                    region_n, l_unroll, t0, t1);
            // print_grid(stdout, t0, t1, l_father_grid);
            ++num_bkernel_;
#endif
            (*opks_[region_n]).bkernel_[0](t0, t1, l_father_grid);
        }
    } else {
        if ((t1 - time_shift_) % l_unroll != 0 || (t0 - time_shift_) % l_unroll != 0) {
        // if (t1 % l_unroll != 0 || t0 % l_unroll != 0) {
#ifdef DEBUG_CUT_T_ALIGNMENT 
            printf("<plan_cut_p> cond_kernel_[%d] l_unroll = %d, lt(%d, %d)\n", 
                    region_n, l_unroll, t0, t1);
            // print_grid(stdout, t0, t1, l_father_grid);
            ++num_cond_kernel_;
#endif
            (*opks_[region_n]).cond_kernel_[0](t0, t1, l_father_grid);
        } else {
            assert((t1 - time_shift_) % l_unroll == 0 && (t0 - time_shift_) % l_unroll == 0); 
#ifdef DEBUG_CUT_T_ALIGNMENT 
            printf("<plan_cut_p> kernel_[%d] l_unroll = %d, lt(%d, %d)\n", 
                    region_n, l_unroll, t0, t1);
            // print_grid(stdout, t0, t1, l_father_grid);
            ++num_kernel_;
#endif
            (*opks_[region_n]).kernel_[0](t0, t1, l_father_grid);
        }
    }
    return;
}

#ifdef DEBUG_CUT_T_ALIGNMENT
#undef DEBUG_CUT_T_ALIGNMENT
#endif
/************************************************************************************
 * Run the Plan for irregular stencil computation                                   *
 ************************************************************************************/

/************************************************************************************
 * Run the Plan for irregular stencil computation                                   * 
 * -- the version for merged kernel                                                 *
 ************************************************************************************/
#define plan_space_can_cut_m(_dim) (cut_lb ? (lb >= 2 * thres && tb + l_padding > dx_recursive_[_dim]) : (tb >= 2 * thres && lb + l_padding > dx_recursive_[_dim]))

#define plan_space_can_cut_mp(_dim) (cut_lb ? ((l_touch_boundary) ? (lb >= 2 * thres && tb > dx_recursive_boundary_[_dim]) : (lb >= 2 * thres && tb + l_padding > dx_recursive_[_dim])) : ((l_touch_boundary) ? (tb >= 2 * thres && lb > dx_recursive_boundary_[_dim]) : (tb >= 2 * thres && lb + l_padding > dx_recursive_[_dim])))

template <int N_RANK> template <typename F> 
inline void Algorithm<N_RANK>::plan_space_cut_m(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n, F const & f)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    plan_bicut_m(l_son->t0, l_son->t1, l_son->grid, region_n, f);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0)
                    plan_bicut_m(l_father->t0, l_father->t1, l_father->grid, region_n, f);
                else
                    cilk_spawn plan_bicut_m(l_father->t0, l_father->t1, l_father->grid, region_n, f);
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                const Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                const int l_padding = 2 * l_slope;
                const bool can_cut = plan_space_can_cut_m(level);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push 
                     * it into the circular queue 
                     */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut! */
                    if (cut_lb) {
                        const int mid = (lb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle triangular minizoid (gray) into 
                         * circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push the left big trapezoid (black)
                         * into circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push the right big trapezoid (black)
                         * into circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                    } /* end if (cut_lb) */
                    else {
                        /* cut_tb */
                        const int mid = (tb/2);
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);
                        const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);

                        /* push left black sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push right black sub-grid into circular queue of (curr_dep) */
                        l_son_grid.x0[level] = ul_start + mid;;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push the middle gray triangular minizoid into 
                         * circular queue of (curr_dep + 1)
                         */
                        l_son_grid.x0[level] = ul_start + mid;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end else (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
//        print_sync(stdout);
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

template <int N_RANK> template <typename F, typename BF> 
inline void Algorithm<N_RANK>::plan_space_cut_mp(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n, F const & f, BF const & bf)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

    /* set up the initial grid */
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                /* spawn all the grids in circular_queue_[curr_dep][] */
#if USE_CILK_FOR 
                /* use cilk_for to spawn all the sub-grid */
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    /* assert all the sub-grid has done N_RANK spatial cuts */
                    assert(l_son->level == -1);
                    plan_bicut_mp(l_son->t0, l_son->t1, l_son->grid, region_n, f, bf);
                } /* end cilk_for */
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                /* use cilk_spawn to spawn all the sub-grid */
                pop_queue(curr_dep_pointer);
                if (queue_len_[curr_dep_pointer] == 0) {
                    plan_bicut_mp(l_father->t0, l_father->t1, l_father->grid, region_n, f, bf);
                } else {
                    cilk_spawn plan_bicut_mp(l_father->t0, l_father->t1, l_father->grid, region_n, f, bf);
                }
#endif
            } else {
                /* performing a space cut on dimension 'level' */
                pop_queue(curr_dep_pointer);
                Grid_Info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int l_slope = slope_[level];
                const int thres = l_slope * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
                const int l_padding = 2 * l_slope;
                const bool l_touch_boundary = touch_boundary(level, lt, l_father_grid);
                const bool can_cut = plan_space_can_cut_mp(level);
                if (!can_cut) {
                    /* if we can't cut into this dimension, just directly push
                     * it into the circular queue
                    */
                    push_queue(curr_dep_pointer, level-1, t0, t1, l_father_grid);
                } else {
                    /* can_cut */
                    if (cut_lb) {
                        /* if cutting lb, there's no initial cut! */
                        assert(lb != phys_length_[level] || l_father_grid.dx0[level] != 0 || l_father_grid.dx1[level] != 0);
                        const int mid = lb/2;
                        Grid_Info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        /* push the middle gray minizoid
                         * into circular queue of (curr_dep) 
                         */
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = l_slope;
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -l_slope;
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* cilk_sync */
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = l_slope;
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        /* push one sub-grid into circular queue of (curr_dep + 1)*/
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -l_slope;
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } /* end if (cut_lb) */
                    else { /* cut_tb */
                        if (lb == phys_length_[level] && l_father_grid.dx0[level] == 0 && l_father_grid.dx1[level] == 0) { /* initial cut on the dimension */
                            assert(l_father_grid.dx0[level] == 0);
                            assert(l_father_grid.dx1[level] == 0);
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* merge the big black trapezoids */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end + (ul_start - l_start) + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push middle minizoid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        } else { /* NOT the initial cut! */
                            const int mid = tb/2;
                            Grid_Info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = -l_slope;
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* push one sub-grid into circular queue of (curr_dep) */
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = l_slope;
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            /* cilk_sync */
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            /* push one sub-grid into circular queue of (curr_dep + 1)*/
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -l_slope;
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = l_slope;
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }                    
                    } /* end if (cut_tb) */
                } /* end if (can_cut) */
            } /* end if (performing a space cut) */
        } /* end while (queue_len_[curr_dep] > 0) */
#if !USE_CILK_FOR
//        print_sync(stdout);
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } /* end for (curr_dep < N_RANK+1) */
}

/* This is the version for interior region cut! */
template <int N_RANK> template <typename F> 
inline void Algorithm<N_RANK>::plan_bicut_m(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n, F const & f)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false;
//    int l_unroll = opks_[region_n].unroll_;

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        bool cut_lb = (lb < tb);
        thres = (slope_[i] * lt);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (plan_space_can_cut_m(i));
    }

    if (sim_can_cut) {
        plan_space_cut_m(t0, t1, grid, region_n, f);
        return;
    } else if (lt > dt_recursive_) {
        /* cut into time */
        assert(lt > dt_recursive_);
        int halflt = lt / 2;
        /* cutting halflt align to unroll_ factor */
        // halflt -= (halflt > l_unroll ? (halflt % l_unroll) : 0);
        Grid_Info<N_RANK> l_son_grid = grid;
        plan_bicut_m(t0, t0+halflt, l_son_grid, region_n, f);

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = grid.dx0[i];
            l_son_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = grid.dx1[i];
        }
        plan_bicut_m(t0+halflt, t1, l_son_grid, region_n, f);
        return;
    } else {
        // base case
#if 1
//        printf("call kernel_ <%d>, l_unroll = %d!\n", region_n, l_unroll);
//        print_grid(stdout, t0, t1, grid);
#endif
//        opks_[region_n].kernel_[0](t0, t1, grid);
        f(t0, t1, grid);
        return;
    }  
}

/* This is the version for boundary region cut! */
template <int N_RANK> template <typename F, typename BF> 
inline void Algorithm<N_RANK>::plan_bicut_mp(int t0, int t1, Grid_Info<N_RANK> const grid, int region_n, F const & f, BF const & bf)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false, call_boundary = false;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    int l_dt_stop;
//    int l_unroll = opks_[region_n].unroll_;

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid);
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        thres = (slope_[i] * lt);
        bool cut_lb = (lb < tb);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (plan_space_can_cut_mp(i));
        call_boundary |= l_touch_boundary;
    }

    if (sim_can_cut) {
        if (call_boundary) 
            plan_space_cut_mp(t0, t1, l_father_grid, region_n, f, bf);
        else
            plan_space_cut_m(t0, t1, l_father_grid, region_n, f);
        return;
    } 

    if (call_boundary) {
        l_dt_stop = dt_recursive_boundary_;
    } else {
        l_dt_stop = dt_recursive_;
    }

    if (lt > l_dt_stop) {
        /* cut into time */
        int halflt = lt / 2;
        /* cut halflt align to unroll_ 
         * for mixed region: we align it to lcm_unroll_;
         * for pure region: we refine it to pgk_[region_n].unroll_;
         */
        // halflt -= (halflt > l_unroll ? (halflt % l_unroll) : 0);
        l_son_grid = l_father_grid;
        if (call_boundary) {
            plan_bicut_mp(t0, t0+halflt, l_son_grid, region_n, f, bf);
        } else {
            plan_bicut_m(t0, t0+halflt, l_son_grid, region_n, f);
        }

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = l_father_grid.dx0[i];
            l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = l_father_grid.dx1[i];
        }
        if (call_boundary) {
            plan_bicut_mp(t0+halflt, t1, l_son_grid, region_n, f, bf);
        } else {
            plan_bicut_m(t0+halflt, t1, l_son_grid, region_n, f);
        }
        return;
    } 

    // base case
    if (call_boundary) {
        /* boundary region */
        // if (t1 - t0 < opks_[region_n].unroll_) {
        // we use lcm_unroll_ instead of l_unroll_ because when we 'gen_plan',
        // in order to get a pure-region, it's possible the bottom bar of current
        // trapezoid is not aligned to the unroll factor if \delta t < lcm_unroll_
        // -- see 'gen_plan' procedure for details!!!
#if 1
//        printf("call boundary_kernel <%d>, l_unroll = %d!\n", region_n, l_unroll);
//        print_grid(stdout, t0, t1, l_father_grid);
#endif
//        opks_[region_n].bkernel_[0](t0, t1, l_father_grid);
        bf(t0, t1, l_father_grid);
    } else {
#if 1 
//        printf("call kernel_ <%d>, l_unroll = %d!\n", region_n, l_unroll);
//        print_grid(stdout, t0, t1, l_father_grid);
#endif
//        opks_[region_n].kernel_[0](t0, t1, l_father_grid);
        f(t0, t1, l_father_grid);
    }
    return;
}

/************************************************************************************
 * Run the Plan for irregular stencil computation                                   *
 ************************************************************************************/

/* This is the version for boundary region cut! */
/* Always cut into the longer bar, and keep the longer bar within the cutting threshold */
template <int N_RANK> template <typename F, typename BF>
inline void Algorithm<N_RANK>::duo_sim_obase_bicut_p(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f, BF const & bf)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false, call_boundary = false;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    int l_dt_stop;

#if STAT
    int l_count_cut = 0;
    int l_bottom_total_area = 1;
    int l_top_total_area = 1;
    int l_total_points;
#endif

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid);
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
        thres = (2 * slope_[i] * lt);
        /* l_father_grid may be mapped to a new region in touch_boundary() */
        /* for the initial cut, we exclude the begining and end point to minimize
         * the overhead on boundary
        */
        /* lb == phys_length_[i] indicates an initial cut! */
        bool cut_lb = (lb >= tb);
        const int l_padding = 2 * slope_[i];
        sim_can_cut = sim_can_cut || (cut_lb ? (l_touch_boundary ? (lb == phys_length_[i] ? (lb >= 2 * thres + l_padding && lb > dx_recursive_boundary_[i]) : (lb >= 2 * thres && lb > dx_recursive_boundary_[i])) : (lb >= 2 * thres && lb + l_padding > dx_recursive_[i])) : (l_touch_boundary ? (tb >= 2 * thres && tb + l_padding > dx_recursive_boundary_[i]) : (tb > 2 * thres && tb + l_padding > dx_recursive_[i])));
        call_boundary |= l_touch_boundary;
#if STAT
        l_count_cut = (l_can_cut ? l_count_cut + 1 : l_count_cut);
        l_bottom_total_area *= lb;
        l_top_total_area *= tb;
#endif
    }

#if STAT
//    l_total_points = l_bottom_total_area * t1 / 3 - l_top_total_area * t0 / 3;
#endif

    if (sim_can_cut) {
        /* cut into space */
        /* push the first l_father_grid that can be cut into the circular queue */
        /* boundary cuts! */
#if STAT
    // sim_count_cut[l_count_cut] = (l_count_cut > 0 ? sim_count_cut[l_count_cut] + 1 : sim_count_cut[l_count_cut]);
        ++sim_count_cut[l_count_cut];
#endif
        if (call_boundary) 
            duo_sim_obase_space_cut_p(t0, t1, l_father_grid, f, bf);
        else
            duo_sim_obase_space_cut(t0, t1, l_father_grid, f);
        return;
    } 

    if (call_boundary)
        l_dt_stop = dt_recursive_boundary_;
    else
        l_dt_stop = dt_recursive_;

    if (lt > l_dt_stop) {
        /* cut into time */
        int halflt = lt / 2;
        l_son_grid = l_father_grid;
        if (call_boundary) {
            duo_sim_obase_bicut_p(t0, t0+halflt, l_son_grid, f, bf);
        } else {
            duo_sim_obase_bicut(t0, t0+halflt, l_son_grid, f);
        }

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = l_father_grid.dx0[i];
            l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = l_father_grid.dx1[i];
        }
        if (call_boundary) {
            duo_sim_obase_bicut_p(t0+halflt, t1, l_son_grid, f, bf);
        } else {
            duo_sim_obase_bicut(t0+halflt, t1, l_son_grid, f);
        }
        return;
    } 

    // if (l_total_area <= Z || base_cube_t) {
        /* for base_cube_t: -- prevent too small time cut! 
         *      (cut_lb && lb > dx_recursive_boundary_ && lb < 2 * thres)
         *  ||  (!cut_lb && tb > dx_recursive_boundary_ && lb < thres)
         */
        // base case
#if DEBUG
        printf("call boundary!\n");
        print_grid(stdout, t0, t1, l_father_grid);
#endif
#if STAT
        ++boundary_region_count;
        boundary_points_count += l_total_points;
#endif
        if (call_boundary) {
            bf(t0, t1, l_father_grid);
//            base_case_kernel_boundary(t0, t1, l_father_grid, bf);
        } else {
            f(t0, t1, l_father_grid);
        }
        return;
}

/* This is the version for interior region cut! */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::sim_obase_bicut(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false;
    Grid_Info<N_RANK> l_son_grid;
#if STAT
    int l_count_cut = 0;
    int l_bottom_total_area = 1;
    int l_top_total_area = 1;
    int l_total_points;
#endif

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres, tb;
        lb = (grid.x1[i] - grid.x0[i]);
        thres = (2 * slope_[i] * lt);
        sim_can_cut = sim_can_cut || (lb >= 2 * thres & lb > dx_recursive_[i]);
        /* as long as there's one dimension can conduct a cut, we conduct a 
         * multi-dimensional cut!
         */
#if STAT
        l_count_cut = (l_can_cut ? l_count_cut+1 : l_count_cut);
#endif
    }

#if STAT
//    l_total_points = l_bottom_total_area * t1 / 3 - l_top_total_area * t0 / 3;
#endif
    if (sim_can_cut) {
        /* cut into space */
#if STAT
    // sim_count_cut[l_count_cut] = (l_count_cut > 0 ? sim_count_cut[l_count_cut] + 1 : sim_count_cut[l_count_cut]);
        ++sim_count_cut[l_count_cut];
#endif
        sim_obase_space_cut(t0, t1, grid, f);
        return;
    // } else if (lt > dt_recursive_ && l_total_points > Z) {
    } else if (lt > dt_recursive_) {
        /* cut into time */
//        assert(dt_recursive_ >= r_t);
        assert(lt > dt_recursive_);
        int halflt = lt / 2;
        l_son_grid = grid;
        sim_obase_bicut(t0, t0+halflt, l_son_grid, f);

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = grid.dx0[i];
            l_son_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = grid.dx1[i];
        }
        sim_obase_bicut(t0+halflt, t1, l_son_grid, f);
        return;
    } else {
        // base case
#if DEBUG
        printf("call interior!\n");
        print_grid(stdout, t0, t1, grid);
        // fprintf(stderr, "l_total_points = %d\n", l_total_points);
#endif
#if STAT
        ++interior_region_count;
        interior_points_count += l_total_points;
#endif
        f(t0, t1, grid);
//        base_case_kernel_interior(t0, t1, grid, f);
        return;
    }  
}

/* This is the version for boundary region cut! */
template <int N_RANK> template <typename F, typename BF>
inline void Algorithm<N_RANK>::sim_obase_bicut_p(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f, BF const & bf)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false, call_boundary = false;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    int l_dt_stop;

#if STAT
    int l_count_cut = 0;
    int l_bottom_total_area = 1;
    int l_top_total_area = 1;
    int l_total_points;
#endif

    for (int i = N_RANK-1; i >= 0; --i) {
        int lb, thres;
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid);
        lb = (grid.x1[i] - grid.x0[i]);
        thres = (2 * slope_[i] * lt);
        /* l_father_grid may be mapped to a new region in touch_boundary() */
        /* for the initial cut, we exclude the begining and end point to minimize
         * the overhead on boundary
        */
        /* lb == phys_length_[i] indicates an initial cut! */
        sim_can_cut = sim_can_cut || (l_touch_boundary ? (lb >= 2 * thres & lb > dx_recursive_boundary_[i]) : (lb >= 2 * thres & lb > dx_recursive_[i]));
        call_boundary |= l_touch_boundary;
#if STAT
        l_count_cut = (l_can_cut ? l_count_cut + 1 : l_count_cut);
        l_bottom_total_area *= lb;
        l_top_total_area *= tb;
#endif
    }

#if STAT
//    l_total_points = l_bottom_total_area * t1 / 3 - l_top_total_area * t0 / 3;
#endif

    if (sim_can_cut) {
        /* cut into space */
        /* push the first l_father_grid that can be cut into the circular queue */
        /* boundary cuts! */
#if STAT
    // sim_count_cut[l_count_cut] = (l_count_cut > 0 ? sim_count_cut[l_count_cut] + 1 : sim_count_cut[l_count_cut]);
        ++sim_count_cut[l_count_cut];
#endif
        if (call_boundary) 
            sim_obase_space_cut_p(t0, t1, l_father_grid, f, bf);
        else
            sim_obase_space_cut(t0, t1, l_father_grid, f);
        return;
    } 

    if (call_boundary)
        l_dt_stop = dt_recursive_boundary_;
    else
        l_dt_stop = dt_recursive_;

    if (lt > l_dt_stop) {
        /* cut into time */
        int halflt = lt / 2;
        l_son_grid = l_father_grid;
        if (call_boundary) {
            sim_obase_bicut_p(t0, t0+halflt, l_son_grid, f, bf);
        } else {
            sim_obase_bicut(t0, t0+halflt, l_son_grid, f);
        }

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = l_father_grid.dx0[i];
            l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = l_father_grid.dx1[i];
        }
        if (call_boundary) {
            sim_obase_bicut_p(t0+halflt, t1, l_son_grid, f, bf);
        } else {
            sim_obase_bicut(t0+halflt, t1, l_son_grid, f);
        }
        return;
    } 

    // if (l_total_area <= Z || base_cube_t) {
        /* for base_cube_t: -- prevent too small time cut! 
         *      (cut_lb && lb > dx_recursive_boundary_ && lb < 2 * thres)
         *  ||  (!cut_lb && tb > dx_recursive_boundary_ && lb < thres)
         */
        // base case
#if DEBUG
        printf("call boundary!\n");
        print_grid(stdout, t0, t1, l_father_grid);
#endif
#if STAT
        ++boundary_region_count;
        boundary_points_count += l_total_points;
#endif
        if (call_boundary) {
            bf(t0, t1, l_father_grid);
//            base_case_kernel_boundary(t0, t1, l_father_grid, bf);
        } else {
            f(t0, t1, l_father_grid);
        }
        return;
}

/* ************************************************************************************** */
/* walk_adaptive() is just for interior region */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::walk_adaptive(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
	/* for the initial cut on each dimension, cut into exact N_CORES pieces,
	   for the rest cut into that dimension, cut into as many as we can!
	 */
	int lt = t1 - t0;
	bool base_cube = (lt <= dt_recursive_); /* dt_recursive_ : temporal dimension stop */
	bool cut_yet = false;
	//int lb[N_RANK];
	//int thres[N_RANK];
	index_info lb, thres;
	Grid_Info<N_RANK> l_grid;

	for (int i = 0; i < N_RANK; ++i) {
		lb[i] = grid.x1[i] - grid.x0[i];
		thres[i] = (initial_cut(i)) ? N_CORES * (2 * slope_[i] * lt) : 2 * (2 * slope_[i] * lt);
		base_cube = base_cube && (lb[i] <= dx_recursive_[i] || lb[i] < thres[i]); 
//		base_cube = base_cube && (lb[i] < thres[i]); 
	}	
	if (base_cube) {
#if DEBUG
        printf("call Adaptive! ");
		print_grid(stdout, t0, t1, grid);
#endif
		base_case_kernel_interior(t0, t1, grid, f);
		return;
	} else  {
		for (int i = N_RANK-1; i >= 0 && !cut_yet; --i) {
			if (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]) { 
//			if (lb[i] >= thres[i]) { 
				l_grid = grid;
				int sep = (initial_cut(i)) ? lb[i]/N_CORES : (2 * slope_[i] * lt);
				int r = (initial_cut(i)) ? N_CORES : (lb[i]/sep);
#if DEBUG
				printf("initial_cut = %s, lb[%d] = %d, sep = %d, r = %d\n", initial_cut(i) ? "True" : "False", i, lb[i], sep, r);
#endif
				int j;
				for (j = 0; j < r-1; ++j) {
					l_grid.x0[i] = grid.x0[i] + sep * j;
					l_grid.dx0[i] = slope_[i];
					l_grid.x1[i] = grid.x0[i] + sep * (j+1);
					l_grid.dx1[i] = -slope_[i];
					cilk_spawn walk_adaptive(t0, t1, l_grid, f);
				}
	//			j_loc = r-1;
				l_grid.x0[i] = grid.x0[i] + sep * (r-1);
				l_grid.dx0[i] = slope_[i];
				l_grid.x1[i] = grid.x1[i];
				l_grid.dx1[i] = -slope_[i];
				cilk_spawn walk_adaptive(t0, t1, l_grid, f);
#if DEBUG
//				print_sync(stdout);
#endif
				cilk_sync;
				if (grid.dx0[i] != slope_[i]) {
					l_grid.x0[i] = grid.x0[i]; l_grid.dx0[i] = grid.dx0[i];
					l_grid.x1[i] = grid.x0[i]; l_grid.dx1[i] = slope_[i];
					cilk_spawn walk_adaptive(t0, t1, l_grid, f);
				}
				for (int j = 1; j < r; ++j) {
					l_grid.x0[i] = grid.x0[i] + sep * j;
					l_grid.dx0[i] = -slope_[i];
					l_grid.x1[i] = grid.x0[i] + sep * j;
					l_grid.dx1[i] = slope_[i];
					cilk_spawn walk_adaptive(t0, t1, l_grid, f);
				}
				if (grid.dx1[i] != -slope_[i]) {
					l_grid.x0[i] = grid.x1[i]; l_grid.dx0[i] = -slope_[i];
					l_grid.x1[i] = grid.x1[i]; l_grid.dx1[i] = grid.dx1[i];
					cilk_spawn walk_adaptive(t0, t1, l_grid, f);
				}
#if 0
				printf("%s:%d cut into %d dim\n", __FUNCTION__, __LINE__, i);
				fflush(stdout);
#endif
				cut_yet = true;
			}/* end if */
		} /* end for */
		if (!cut_yet && lt > dt_recursive_) {
			int halflt = lt / 2;
			l_grid = grid;
			walk_adaptive(t0, t0+halflt, l_grid, f);
#if DEBUG
//			print_sync(stdout);
#endif
			for (int i = 0; i < N_RANK; ++i) {
				l_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
				l_grid.dx0[i] = grid.dx0[i];
				l_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
				l_grid.dx1[i] = grid.dx1[i];
			}
			walk_adaptive(t0+halflt, t1, l_grid, f);
#if 0
			printf("%s:%d cut into time dim\n", __FUNCTION__, __LINE__);
			fflush(stdout);
#endif
			cut_yet = true;
		}
		assert(cut_yet);
		return;
	}
}

/* walk_ncores_boundary_p() will be called for -split-shadow mode */
template <int N_RANK> template <typename F, typename BF>
inline void Algorithm<N_RANK>::walk_bicut_boundary_p(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f, BF const & bf)
{
	/* cut into exact N_CORES pieces */
	/* Indirect memory access is expensive */
	int lt = t1 - t0;
	bool base_cube = (lt <= dt_recursive_); /* dt_recursive_ : temporal dimension stop */
	bool can_cut = false, call_boundary = false;
	index_info lb, thres;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    bool l_touch_boundary[N_RANK];
    int l_dt_stop;

	for (int i = 0; i < N_RANK; ++i) {
        l_touch_boundary[i] = touch_boundary(i, lt, l_father_grid);
		lb[i] = (l_father_grid.x1[i] - l_father_grid.x0[i]);
		thres[i] = 2 * (2 * slope_[i] * lt);
		call_boundary |= l_touch_boundary[i];
	}	

	for (int i = N_RANK-1; i >= 0; --i) {
		can_cut = (l_touch_boundary[i]) ? (lb[i] >= thres[i] && lb[i] > dx_recursive_boundary_[i]) : (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]);
		if (can_cut) { 
			l_son_grid = l_father_grid;
            int sep = (int)lb[i]/2;
            int r = 2;
			int l_start = (l_father_grid.x0[i]);
			int l_end = (l_father_grid.x1[i]);

			l_son_grid.x0[i] = l_start;
			l_son_grid.dx0[i] = slope_[i];
			l_son_grid.x1[i] = l_start + sep;
			l_son_grid.dx1[i] = -slope_[i];
            if (call_boundary) {
                cilk_spawn walk_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
            } else {
                cilk_spawn walk_bicut(t0, t1, l_son_grid, f);
            }

			l_son_grid.x0[i] = l_start + sep;
			l_son_grid.dx0[i] = slope_[i];
			l_son_grid.x1[i] = l_end;
			l_son_grid.dx1[i] = -slope_[i];
            if (call_boundary) {
                walk_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
            } else {
                walk_bicut(t0, t1, l_son_grid, f);
            }
#if DEBUG
			print_sync(stdout);
#endif
			cilk_sync;

			l_son_grid.x0[i] = l_start + sep;
			l_son_grid.dx0[i] = -slope_[i];
			l_son_grid.x1[i] = l_start + sep;
			l_son_grid.dx1[i] = slope_[i];
            if (call_boundary) {
                cilk_spawn walk_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
            } else {
                cilk_spawn walk_bicut(t0, t1, l_son_grid, f);
            }

			if (l_start == phys_grid_.x0[i] && l_end == phys_grid_.x1[i]) {
        //        printf("merge triagles!\n");
				l_son_grid.x0[i] = l_end;
				l_son_grid.dx0[i] = -slope_[i];
				l_son_grid.x1[i] = l_end;
				l_son_grid.dx1[i] = slope_[i];
                if (call_boundary) {
                    cilk_spawn walk_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
                } else {
                    cilk_spawn walk_bicut(t0, t1, l_son_grid, f);
                }
			} else {
				if (l_father_grid.dx0[i] != slope_[i]) {
					l_son_grid.x0[i] = l_start; 
					l_son_grid.dx0[i] = l_father_grid.dx0[i];
					l_son_grid.x1[i] = l_start; 
					l_son_grid.dx1[i] = slope_[i];
                    if (call_boundary) {
                        cilk_spawn walk_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
                    } else {
                        cilk_spawn walk_bicut(t0, t1, l_son_grid, f);
                    }
				}
				if (l_father_grid.dx1[i] != -slope_[i]) {
					l_son_grid.x0[i] = l_end; 
					l_son_grid.dx0[i] = -slope_[i];
					l_son_grid.x1[i] = l_end; 
					l_son_grid.dx1[i] = l_father_grid.dx1[i];
                    if (call_boundary) {
                        cilk_spawn walk_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
                    } else {
                        cilk_spawn walk_bicut(t0, t1, l_son_grid, f);
                    }
				}
			}
	        return;
		}/* end if */
	} /* end for */
    if (call_boundary)
        l_dt_stop = dt_recursive_boundary_;
    else
        l_dt_stop = dt_recursive_;
	if (lt > l_dt_stop) {
		int halflt = lt / 2;
		l_son_grid = l_father_grid;
        if (call_boundary) {
            walk_bicut_boundary_p(t0, t0+halflt, l_son_grid, f, bf);
        } else {
            walk_bicut(t0, t0+halflt, l_son_grid, f);
        }
#if DEBUG
		print_sync(stdout);
#endif
		for (int i = 0; i < N_RANK; ++i) {
			l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
			l_son_grid.dx0[i] = l_father_grid.dx0[i];
			l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
			l_son_grid.dx1[i] = l_father_grid.dx1[i];
		}
        if (call_boundary) { 
            walk_bicut_boundary_p(t0+halflt, t1, l_son_grid, f, bf);
        } else {
            walk_bicut(t0+halflt, t1, l_son_grid, f);
        }
	    return;
	} 
    // base cube
	if (call_boundary) {
        /* for periodic stencils, all elements falling into boundary region
         * requires special treatment of 'BF' (usually requires modulo operation
         * to wrap-up the index)
         */
#if DEBUG
        printf("call Boundary! ");
        print_grid(stdout, t0, t1, l_father_grid);
#endif
		base_case_kernel_boundary(t0, t1, l_father_grid, bf);
    } else {
#if DEBUG
        printf("call Interior! ");
	    print_grid(stdout, t0, t1, l_father_grid);
#endif
	    base_case_kernel_interior(t0, t1, l_father_grid, f);
    }
    return;
}


/* walk_ncores_boundary_p() will be called for -split-shadow mode */
template <int N_RANK> template <typename F, typename BF>
inline void Algorithm<N_RANK>::walk_ncores_boundary_p(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f, BF const & bf)
{
	/* cut into exact N_CORES pieces */
	/* Indirect memory access is expensive */
	int lt = t1 - t0;
	bool base_cube = (lt <= dt_recursive_); /* dt_recursive_ : temporal dimension stop */
	bool cut_yet = false, can_cut = false, call_boundary = false;
	index_info lb, thres;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    bool l_touch_boundary[N_RANK];

	for (int i = 0; i < N_RANK; ++i) {
        l_touch_boundary[i] = touch_boundary(i, lt, l_father_grid);
		lb[i] = (l_father_grid.x1[i] - l_father_grid.x0[i]);
		thres[i] = (initial_cut(i)) ?  N_CORES * (2 * slope_[i] * lt) : 2 * (2 * slope_[i] * lt);
		call_boundary |= l_touch_boundary[i];
		if (l_touch_boundary[i])
			base_cube = base_cube && (lb[i] <= dx_recursive_boundary_[i] || lb[i] < thres[i]); 
		else 
			base_cube = base_cube && (lb[i] <= dx_recursive_[i] || lb[i] < thres[i]); 
	}	

	if (base_cube) {
		if (call_boundary) {
            /* for periodic stencils, all elements falling into boundary region
             * requires special treatment of 'BF' (usually requires modulo operation
             * to wrap-up the index)
             */
#if DEBUG
	        printf("call Boundary! ");
            print_grid(stdout, t0, t1, l_father_grid);
#endif
			base_case_kernel_boundary(t0, t1, l_father_grid, bf);
        } else {
#if DEBUG
            printf("call Interior! ");
	    	print_grid(stdout, t0, t1, l_father_grid);
#endif
			base_case_kernel_interior(t0, t1, l_father_grid, f);
        }
		return;
	} else  {
		for (int i = N_RANK-1; i >= 0 && !cut_yet; --i) {
			can_cut = (l_touch_boundary[i]) ? (lb[i] >= thres[i] && lb[i] > dx_recursive_boundary_[i]) : (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]);
			if (can_cut) { 
				l_son_grid = l_father_grid;
                int sep = (initial_cut(i)) ? lb[i]/N_CORES : (2 * slope_[i] * lt);
                int r = (initial_cut(i)) ? N_CORES : (lb[i]/sep);
				int l_start = (l_father_grid.x0[i]);
				int l_end = (l_father_grid.x1[i]);
				int j;
				for (j = 0; j < r-1; ++j) {
					l_son_grid.x0[i] = l_start + sep * j;
					l_son_grid.dx0[i] = slope_[i];
					l_son_grid.x1[i] = l_start + sep * (j+1);
					l_son_grid.dx1[i] = -slope_[i];
                    if (call_boundary) {
                        cilk_spawn walk_ncores_boundary_p(t0, t1, l_son_grid, f, bf);
                    } else {
                        cilk_spawn walk_adaptive(t0, t1, l_son_grid, f);
                    }
				}
				l_son_grid.x0[i] = l_start + sep * j;
				l_son_grid.dx0[i] = slope_[i];
				l_son_grid.x1[i] = l_end;
				l_son_grid.dx1[i] = -slope_[i];
                if (call_boundary) {
                    walk_ncores_boundary_p(t0, t1, l_son_grid, f, bf);
                } else {
                    walk_adaptive(t0, t1, l_son_grid, f);
                }
#if DEBUG
//				print_sync(stdout);
#endif
				cilk_sync;
				for (j = 1; j < r; ++j) {
					l_son_grid.x0[i] = l_start + sep * j;
					l_son_grid.dx0[i] = -slope_[i];
					l_son_grid.x1[i] = l_start + sep * j;
					l_son_grid.dx1[i] = slope_[i];
                    if (call_boundary) {
                        cilk_spawn walk_ncores_boundary_p(t0, t1, l_son_grid, f, bf);
                    } else {
                        cilk_spawn walk_adaptive(t0, t1, l_son_grid, f);
                    }
				}
				if (l_start == phys_grid_.x0[i] && l_end == phys_grid_.x1[i]) {
            //        printf("merge triagles!\n");
					l_son_grid.x0[i] = l_end;
					l_son_grid.dx0[i] = -slope_[i];
					l_son_grid.x1[i] = l_end;
					l_son_grid.dx1[i] = slope_[i];
                    if (call_boundary) {
                        cilk_spawn walk_ncores_boundary_p(t0, t1, l_son_grid, f, bf);
                    } else {
                        cilk_spawn walk_adaptive(t0, t1, l_son_grid, f);
                    }
				} else {
					if (l_father_grid.dx0[i] != slope_[i]) {
						l_son_grid.x0[i] = l_start; 
						l_son_grid.dx0[i] = l_father_grid.dx0[i];
						l_son_grid.x1[i] = l_start; 
						l_son_grid.dx1[i] = slope_[i];
                        if (call_boundary) {
                            cilk_spawn walk_ncores_boundary_p(t0, t1, l_son_grid, f, bf);
                        } else {
                            cilk_spawn walk_adaptive(t0, t1, l_son_grid, f);
                        }
					}
					if (l_father_grid.dx1[i] != -slope_[i]) {
						l_son_grid.x0[i] = l_end; 
						l_son_grid.dx0[i] = -slope_[i];
						l_son_grid.x1[i] = l_end; 
						l_son_grid.dx1[i] = l_father_grid.dx1[i];
                        if (call_boundary) {
                            cilk_spawn walk_ncores_boundary_p(t0, t1, l_son_grid, f, bf);
                        } else {
                            cilk_spawn walk_adaptive(t0, t1, l_son_grid, f);
                        }
					}
				}
				cut_yet = true;
			}/* end if */
		} /* end for */
		if (!cut_yet && lt > dt_recursive_) {
			int halflt = lt / 2;
			l_son_grid = l_father_grid;
            if (call_boundary) {
                walk_ncores_boundary_p(t0, t0+halflt, l_son_grid, f, bf);
            } else {
                walk_adaptive(t0, t0+halflt, l_son_grid, f);
            }
#if DEBUG
//			print_sync(stdout);
#endif
			for (int i = 0; i < N_RANK; ++i) {
				l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
				l_son_grid.dx0[i] = l_father_grid.dx0[i];
				l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
				l_son_grid.dx1[i] = l_father_grid.dx1[i];
			}
            if (call_boundary) { 
                walk_ncores_boundary_p(t0+halflt, t1, l_son_grid, f, bf);
            } else {
                walk_adaptive(t0+halflt, t1, l_son_grid, f);
            }
			cut_yet = true;
		}
		assert(cut_yet);
		return;
	}
}

/* this is for interior region */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::obase_bicut(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
	/* for the initial cut on each dimension, cut into exact N_CORES pieces,
	   for the rest cut into that dimension, cut into as many as we can!
	 */
	int lt = t1 - t0;
	index_info lb, thres;
	Grid_Info<N_RANK> l_grid;

	for (int i = 0; i < N_RANK; ++i) {
		lb[i] = grid.x1[i] - grid.x0[i];
		thres[i] = 2 * (2 * slope_[i] * lt);
	}	
	for (int i = N_RANK-1; i >= 0; --i) {
		if (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]) { 
			l_grid = grid;
			int sep = (int)lb[i]/2;
			int r = 2;
#if DEBUG
			printf("initial_cut = %s, lb[%d] = %d, sep = %d, r = %d\n", initial_cut(i) ? "True" : "False", i, lb[i], sep, r);
#endif
			l_grid.x0[i] = grid.x0[i];
			l_grid.dx0[i] = slope_[i];
			l_grid.x1[i] = grid.x0[i] + sep;
			l_grid.dx1[i] = -slope_[i];
			cilk_spawn obase_bicut(t0, t1, l_grid, f);

			l_grid.x0[i] = grid.x0[i] + sep;
			l_grid.dx0[i] = slope_[i];
			l_grid.x1[i] = grid.x1[i];
			l_grid.dx1[i] = -slope_[i];
			cilk_spawn obase_bicut(t0, t1, l_grid, f);
#if DEBUG
//			print_sync(stdout);
#endif
			cilk_sync;
			if (grid.dx0[i] != slope_[i]) {
				l_grid.x0[i] = grid.x0[i]; l_grid.dx0[i] = grid.dx0[i];
				l_grid.x1[i] = grid.x0[i]; l_grid.dx1[i] = slope_[i];
				cilk_spawn obase_bicut(t0, t1, l_grid, f);
			}

			l_grid.x0[i] = grid.x0[i] + sep;
			l_grid.dx0[i] = -slope_[i];
			l_grid.x1[i] = grid.x0[i] + sep;
			l_grid.dx1[i] = slope_[i];
			cilk_spawn obase_bicut(t0, t1, l_grid, f);

			if (grid.dx1[i] != -slope_[i]) {
				l_grid.x0[i] = grid.x1[i]; l_grid.dx0[i] = -slope_[i];
				l_grid.x1[i] = grid.x1[i]; l_grid.dx1[i] = grid.dx1[i];
				cilk_spawn obase_bicut(t0, t1, l_grid, f);
			}
#if DEBUG
			printf("%s:%d cut into %d dim\n", __FUNCTION__, __LINE__, i);
			fflush(stdout);
#endif
            return;
		}/* end if */
	} /* end for */
	if (lt > dt_recursive_) {
		int halflt = lt / 2;
		l_grid = grid;
		obase_bicut(t0, t0+halflt, l_grid, f);
#if DEBUG
//		print_sync(stdout);
#endif
		for (int i = 0; i < N_RANK; ++i) {
			l_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
			l_grid.dx0[i] = grid.dx0[i];
			l_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
			l_grid.dx1[i] = grid.dx1[i];
		}
		obase_bicut(t0+halflt, t1, l_grid, f);
#if DEBUG 
		printf("%s:%d cut into time dim\n", __FUNCTION__, __LINE__);
		fflush(stdout);
#endif
        return;
	}
#if DEBUG
    printf("call obase_bicut! ");
    print_grid(stdout, t0, t1, grid);
#endif
	f(t0, t1, grid);
	return;
}


/* this is for interior region */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::obase_m(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
	/* for the initial cut on each dimension, cut into exact N_CORES pieces,
	   for the rest cut into that dimension, cut into as many as we can!
	 */
	int lt = t1 - t0;
	bool base_cube = (lt <= dt_recursive_); /* dt_recursive_ : temporal dimension stop */
	bool cut_yet = false;
	//int lb[N_RANK];
	//int thres[N_RANK];
	index_info lb, thres, tb;
    bool cut_lb[N_RANK];
	Grid_Info<N_RANK> l_grid;

	for (int i = 0; i < N_RANK; ++i) {
		lb[i] = grid.x1[i] - grid.x0[i];
        tb[i] = (grid.x1[i] + grid.dx1[i] * lt) - (grid.x0[i] + grid.dx0[i] * lt);
        cut_lb[i] = (grid.dx0[i] >= 0 && grid.dx1[i] <= 0);
		thres[i] = 2 * (2 * slope_[i] * lt);
		base_cube = base_cube && (lb[i] <= dx_recursive_[i] || lb[i] < thres[i]); 
	}	
	if (base_cube) {
#if DEBUG
        printf("call Obase_m! ");
		print_grid(stdout, t0, t1, grid);
#endif
		f(t0, t1, grid);
		return;
	} else  {
		for (int i = N_RANK-1; i >= 0 && !cut_yet; --i) {
			if (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]) { 
				l_grid = grid;
				int sep = (2 * slope_[i] * lt);
				int r = (lb[i]/sep);
#if DEBUG
				printf("initial_cut = %s, lb[%d] = %d, sep = %d, r = %d\n", initial_cut(i) ? "True" : "False", i, lb[i], sep, r);
#endif
				int j;
				for (j = 0; j < r-1; ++j) {
					l_grid.x0[i] = grid.x0[i] + sep * j;
					l_grid.dx0[i] = slope_[i];
					l_grid.x1[i] = grid.x0[i] + sep * (j+1);
					l_grid.dx1[i] = -slope_[i];
					cilk_spawn obase_m(t0, t1, l_grid, f);
				}
	//			j_loc = r-1;
				l_grid.x0[i] = grid.x0[i] + sep * (r-1);
				l_grid.dx0[i] = slope_[i];
				l_grid.x1[i] = grid.x1[i];
				l_grid.dx1[i] = -slope_[i];
				cilk_spawn obase_m(t0, t1, l_grid, f);
#if DEBUG
//				print_sync(stdout);
#endif
				cilk_sync;
				if (grid.dx0[i] != slope_[i]) {
					l_grid.x0[i] = grid.x0[i]; l_grid.dx0[i] = grid.dx0[i];
					l_grid.x1[i] = grid.x0[i]; l_grid.dx1[i] = slope_[i];
					cilk_spawn obase_m(t0, t1, l_grid, f);
				}
				for (int j = 1; j < r; ++j) {
					l_grid.x0[i] = grid.x0[i] + sep * j;
					l_grid.dx0[i] = -slope_[i];
					l_grid.x1[i] = grid.x0[i] + sep * j;
					l_grid.dx1[i] = slope_[i];
					cilk_spawn obase_m(t0, t1, l_grid, f);
				}
				if (grid.dx1[i] != -slope_[i]) {
					l_grid.x0[i] = grid.x1[i]; l_grid.dx0[i] = -slope_[i];
					l_grid.x1[i] = grid.x1[i]; l_grid.dx1[i] = grid.dx1[i];
					cilk_spawn obase_m(t0, t1, l_grid, f);
				}
#if 0
				printf("%s:%d cut into %d dim\n", __FUNCTION__, __LINE__, i);
				fflush(stdout);
#endif
				cut_yet = true;
			}/* end if */
		} /* end for */
		if (!cut_yet && lt > dt_recursive_) {
			int halflt = lt / 2;
			l_grid = grid;
			obase_m(t0, t0+halflt, l_grid, f);
#if DEBUG
//			print_sync(stdout);
#endif
			for (int i = 0; i < N_RANK; ++i) {
				l_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
				l_grid.dx0[i] = grid.dx0[i];
				l_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
				l_grid.dx1[i] = grid.dx1[i];
			}
			obase_m(t0+halflt, t1, l_grid, f);
#if 0
			printf("%s:%d cut into time dim\n", __FUNCTION__, __LINE__);
			fflush(stdout);
#endif
			cut_yet = true;
		}
		assert(cut_yet);
		return;
	}
}

/* this is for interior region */
template <int N_RANK> template <typename F>
inline void Algorithm<N_RANK>::obase_adaptive(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f)
{
	/* for the initial cut on each dimension, cut into exact N_CORES pieces,
	   for the rest cut into that dimension, cut into as many as we can!
	 */
	int lt = t1 - t0;
	bool base_cube = (lt <= dt_recursive_); /* dt_recursive_ : temporal dimension stop */
	bool cut_yet = false;
	//int lb[N_RANK];
	//int thres[N_RANK];
	index_info lb, thres;
	Grid_Info<N_RANK> l_grid;

	for (int i = 0; i < N_RANK; ++i) {
		lb[i] = grid.x1[i] - grid.x0[i];
		thres[i] = (initial_cut(i)) ? N_CORES * (2 * slope_[i] * lt) : 2 * (2 * slope_[i] * lt);
		base_cube = base_cube && (lb[i] <= dx_recursive_[i] || lb[i] < thres[i]); 
	}	
	if (base_cube) {
#if DEBUG
        printf("call Adaptive! ");
		print_grid(stdout, t0, t1, grid);
#endif
		f(t0, t1, grid);
		return;
	} else  {
		for (int i = N_RANK-1; i >= 0 && !cut_yet; --i) {
			if (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]) { 
				l_grid = grid;
				int sep = (initial_cut(i)) ? lb[i]/N_CORES : (2 * slope_[i] * lt);
				int r = (initial_cut(i)) ? N_CORES : (lb[i]/sep);
#if DEBUG
				printf("initial_cut = %s, lb[%d] = %d, sep = %d, r = %d\n", initial_cut(i) ? "True" : "False", i, lb[i], sep, r);
#endif
				int j;
				for (j = 0; j < r-1; ++j) {
					l_grid.x0[i] = grid.x0[i] + sep * j;
					l_grid.dx0[i] = slope_[i];
					l_grid.x1[i] = grid.x0[i] + sep * (j+1);
					l_grid.dx1[i] = -slope_[i];
					cilk_spawn obase_adaptive(t0, t1, l_grid, f);
				}
	//			j_loc = r-1;
				l_grid.x0[i] = grid.x0[i] + sep * (r-1);
				l_grid.dx0[i] = slope_[i];
				l_grid.x1[i] = grid.x1[i];
				l_grid.dx1[i] = -slope_[i];
				cilk_spawn obase_adaptive(t0, t1, l_grid, f);
#if DEBUG
//				print_sync(stdout);
#endif
				cilk_sync;
				if (grid.dx0[i] != slope_[i]) {
					l_grid.x0[i] = grid.x0[i]; l_grid.dx0[i] = grid.dx0[i];
					l_grid.x1[i] = grid.x0[i]; l_grid.dx1[i] = slope_[i];
					cilk_spawn obase_adaptive(t0, t1, l_grid, f);
				}
				for (int j = 1; j < r; ++j) {
					l_grid.x0[i] = grid.x0[i] + sep * j;
					l_grid.dx0[i] = -slope_[i];
					l_grid.x1[i] = grid.x0[i] + sep * j;
					l_grid.dx1[i] = slope_[i];
					cilk_spawn obase_adaptive(t0, t1, l_grid, f);
				}
				if (grid.dx1[i] != -slope_[i]) {
					l_grid.x0[i] = grid.x1[i]; l_grid.dx0[i] = -slope_[i];
					l_grid.x1[i] = grid.x1[i]; l_grid.dx1[i] = grid.dx1[i];
					cilk_spawn obase_adaptive(t0, t1, l_grid, f);
				}
#if 0
				printf("%s:%d cut into %d dim\n", __FUNCTION__, __LINE__, i);
				fflush(stdout);
#endif
				cut_yet = true;
			}/* end if */
		} /* end for */
		if (!cut_yet && lt > dt_recursive_) {
			int halflt = lt / 2;
			l_grid = grid;
			obase_adaptive(t0, t0+halflt, l_grid, f);
#if DEBUG
//			print_sync(stdout);
#endif
			for (int i = 0; i < N_RANK; ++i) {
				l_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
				l_grid.dx0[i] = grid.dx0[i];
				l_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
				l_grid.dx1[i] = grid.dx1[i];
			}
			obase_adaptive(t0+halflt, t1, l_grid, f);
#if 0
			printf("%s:%d cut into time dim\n", __FUNCTION__, __LINE__);
			fflush(stdout);
#endif
			cut_yet = true;
		}
		assert(cut_yet);
		return;
	}
}

/* this is the version for executable spec!!! */
template <int N_RANK> template <typename BF>
inline void Algorithm<N_RANK>::obase_bicut_boundary_p(int t0, int t1, Grid_Info<N_RANK> const grid, BF const & bf)
{
	/* cut into exact N_CORES pieces */
	/* Indirect memory access is expensive */
	int lt = t1 - t0;
	bool can_cut = false, call_boundary = false;
	index_info lb, thres;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    bool l_touch_boundary[N_RANK];

	for (int i = 0; i < N_RANK; ++i) {
        l_touch_boundary[i] = touch_boundary(i, lt, l_father_grid);
		lb[i] = (l_father_grid.x1[i] - l_father_grid.x0[i]);
		thres[i] = 2 * (2 * slope_[i] * lt);
		call_boundary = call_boundary || l_touch_boundary[i];
	}	

	for (int i = N_RANK-1; i >= 0; --i) {
		can_cut = (l_touch_boundary[i]) ? (lb[i] >= thres[i] && lb[i] > dx_recursive_boundary_[i]) : (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]);
		if (can_cut) { 
			l_son_grid = l_father_grid;
            int sep = (int)lb[i]/2;
            int r = 2;
			int l_start = (l_father_grid.x0[i]);
			int l_end = (l_father_grid.x1[i]);
			int j;

			l_son_grid.x0[i] = l_start;
			l_son_grid.dx0[i] = slope_[i];
			l_son_grid.x1[i] = l_start + sep;
			l_son_grid.dx1[i] = -slope_[i];
            cilk_spawn obase_bicut_boundary_p(t0, t1, l_son_grid, bf);

			l_son_grid.x0[i] = l_start + sep * j;
			l_son_grid.dx0[i] = slope_[i];
			l_son_grid.x1[i] = l_end;
			l_son_grid.dx1[i] = -slope_[i];
            obase_bicut_boundary_p(t0, t1, l_son_grid, bf);
#if DEBUG
//			print_sync(stdout);
#endif
			cilk_sync;
			l_son_grid.x0[i] = l_start + sep;
			l_son_grid.dx0[i] = -slope_[i];
			l_son_grid.x1[i] = l_start + sep;
			l_son_grid.dx1[i] = slope_[i];
            cilk_spawn obase_bicut_boundary_p(t0, t1, l_son_grid, bf);
			if (l_start == phys_grid_.x0[i] && l_end == phys_grid_.x1[i]) {
        //        printf("merge triagles!\n");
				l_son_grid.x0[i] = l_end;
				l_son_grid.dx0[i] = -slope_[i];
				l_son_grid.x1[i] = l_end;
				l_son_grid.dx1[i] = slope_[i];
                cilk_spawn obase_bicut_boundary_p(t0, t1, l_son_grid, bf);
			} else {
				if (l_father_grid.dx0[i] != slope_[i]) {
					l_son_grid.x0[i] = l_start; 
					l_son_grid.dx0[i] = l_father_grid.dx0[i];
					l_son_grid.x1[i] = l_start; 
					l_son_grid.dx1[i] = slope_[i];
                    cilk_spawn obase_bicut_boundary_p(t0, t1, l_son_grid, bf);
				}
				if (l_father_grid.dx1[i] != -slope_[i]) {
					l_son_grid.x0[i] = l_end; 
					l_son_grid.dx0[i] = -slope_[i];
					l_son_grid.x1[i] = l_end; 
					l_son_grid.dx1[i] = l_father_grid.dx1[i];
                    cilk_spawn obase_bicut_boundary_p(t0, t1, l_son_grid, bf);
				}
			}
            return;
		}/* end if */
	} /* end for */
	if (lt > dt_recursive_boundary_) {
		int halflt = lt / 2;
		l_son_grid = l_father_grid;
        obase_bicut_boundary_p(t0, t0+halflt, l_son_grid, bf);
#if DEBUG
//		print_sync(stdout);
#endif
		for (int i = 0; i < N_RANK; ++i) {
			l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
			l_son_grid.dx0[i] = l_father_grid.dx0[i];
			l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
			l_son_grid.dx1[i] = l_father_grid.dx1[i];
		}
        obase_bicut_boundary_p(t0+halflt, t1, l_son_grid, bf);
        return;
	}
	base_case_kernel_boundary(t0, t1, l_father_grid, bf);
	return;
}


/* this is the version for executable spec!!! */
template <int N_RANK> template <typename BF>
inline void Algorithm<N_RANK>::obase_boundary_p(int t0, int t1, Grid_Info<N_RANK> const grid, BF const & bf)
{
	/* cut into exact N_CORES pieces */
	/* Indirect memory access is expensive */
	int lt = t1 - t0;
	bool base_cube = (lt <= dt_recursive_); /* dt_recursive_ : temporal dimension stop */
	bool cut_yet = false, can_cut = false, call_boundary = false;
	index_info lb, thres;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    bool l_touch_boundary[N_RANK];

	for (int i = 0; i < N_RANK; ++i) {
        l_touch_boundary[i] = touch_boundary(i, lt, l_father_grid);
		lb[i] = (l_father_grid.x1[i] - l_father_grid.x0[i]);
		thres[i] = (initial_cut(i)) ?  N_CORES * (2 * slope_[i] * lt) : 2 * (2 * slope_[i] * lt);
		if (l_touch_boundary[i])
			base_cube = base_cube && (lb[i] <= dx_recursive_boundary_[i] || lb[i] < thres[i]); 
		else 
			base_cube = base_cube && (lb[i] <= dx_recursive_[i] || lb[i] < thres[i]); 
		call_boundary = call_boundary || l_touch_boundary[i];
	}	

	if (base_cube) {
		base_case_kernel_boundary(t0, t1, l_father_grid, bf);
		return;
	} else  {
		for (int i = N_RANK-1; i >= 0 && !cut_yet; --i) {
			can_cut = (l_touch_boundary[i]) ? (lb[i] >= thres[i] && lb[i] > dx_recursive_boundary_[i]) : (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]);
			if (can_cut) { 
				l_son_grid = l_father_grid;
                int sep = (initial_cut(i)) ? lb[i]/N_CORES : (2 * slope_[i] * lt);
                //int r = (initial_cut(i)) ? N_CORES : (lb[i]/sep);
                int r = lb[i]/sep;
				int l_start = (l_father_grid.x0[i]);
				int l_end = (l_father_grid.x1[i]);
				int j;
				for (j = 0; j < r-1; ++j) {
					l_son_grid.x0[i] = l_start + sep * j;
					l_son_grid.dx0[i] = slope_[i];
					l_son_grid.x1[i] = l_start + sep * (j+1);
					l_son_grid.dx1[i] = -slope_[i];
                    cilk_spawn obase_boundary_p(t0, t1, l_son_grid, bf);
				}
				l_son_grid.x0[i] = l_start + sep * j;
				l_son_grid.dx0[i] = slope_[i];
				l_son_grid.x1[i] = l_end;
				l_son_grid.dx1[i] = -slope_[i];
                obase_boundary_p(t0, t1, l_son_grid, bf);
#if DEBUG
//				print_sync(stdout);
#endif
				cilk_sync;
				for (j = 1; j < r; ++j) {
					l_son_grid.x0[i] = l_start + sep * j;
					l_son_grid.dx0[i] = -slope_[i];
					l_son_grid.x1[i] = l_start + sep * j;
					l_son_grid.dx1[i] = slope_[i];
                    cilk_spawn obase_boundary_p(t0, t1, l_son_grid, bf);
				}
				if (l_start == phys_grid_.x0[i] && l_end == phys_grid_.x1[i]) {
            //        printf("merge triagles!\n");
					l_son_grid.x0[i] = l_end;
					l_son_grid.dx0[i] = -slope_[i];
					l_son_grid.x1[i] = l_end;
					l_son_grid.dx1[i] = slope_[i];
                    cilk_spawn obase_boundary_p(t0, t1, l_son_grid, bf);
				} else {
					if (l_father_grid.dx0[i] != slope_[i]) {
						l_son_grid.x0[i] = l_start; 
						l_son_grid.dx0[i] = l_father_grid.dx0[i];
						l_son_grid.x1[i] = l_start; 
						l_son_grid.dx1[i] = slope_[i];
                        cilk_spawn obase_boundary_p(t0, t1, l_son_grid, bf);
					}
					if (l_father_grid.dx1[i] != -slope_[i]) {
						l_son_grid.x0[i] = l_end; 
						l_son_grid.dx0[i] = -slope_[i];
						l_son_grid.x1[i] = l_end; 
						l_son_grid.dx1[i] = l_father_grid.dx1[i];
                        cilk_spawn obase_boundary_p(t0, t1, l_son_grid, bf);
					}
				}
				cut_yet = true;
			}/* end if */
		} /* end for */
		if (!cut_yet && lt > dt_recursive_) {
			int halflt = lt / 2;
			l_son_grid = l_father_grid;
            obase_boundary_p(t0, t0+halflt, l_son_grid, bf);
#if DEBUG
//			print_sync(stdout);
#endif
			for (int i = 0; i < N_RANK; ++i) {
				l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
				l_son_grid.dx0[i] = l_father_grid.dx0[i];
				l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
				l_son_grid.dx1[i] = l_father_grid.dx1[i];
			}
            obase_boundary_p(t0+halflt, t1, l_son_grid, bf);
			cut_yet = true;
		}
		assert(cut_yet);
		return;
	}
}

/* this is for optimizing base case!!! */
template <int N_RANK> template <typename F, typename BF>
inline void Algorithm<N_RANK>::obase_bicut_boundary_p(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f, BF const & bf)
{
	/* cut into exact N_CORES pieces */
	/* Indirect memory access is expensive */
	int lt = t1 - t0;
	bool can_cut = false, call_boundary = false;
	index_info lb, thres;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    bool l_touch_boundary[N_RANK];
    int l_dt_stop;

	for (int i = 0; i < N_RANK; ++i) {
        l_touch_boundary[i] = touch_boundary(i, lt, l_father_grid);
		lb[i] = (l_father_grid.x1[i] - l_father_grid.x0[i]);
		thres[i] = 2 * (2 * slope_[i] * lt);
		call_boundary = call_boundary || l_touch_boundary[i];
	}	

	for (int i = N_RANK-1; i >= 0; --i) {
		can_cut = (l_touch_boundary[i]) ? (lb[i] >= thres[i] && lb[i] > dx_recursive_boundary_[i]) : (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]);
		if (can_cut) { 
            l_son_grid = l_father_grid;
            int sep = lb[i]/2;
            int r = 2;
			int l_start = (l_father_grid.x0[i]);
			int l_end = (l_father_grid.x1[i]);

			l_son_grid.x0[i] = l_start;
			l_son_grid.dx0[i] = slope_[i];
			l_son_grid.x1[i] = l_start + sep;
			l_son_grid.dx1[i] = -slope_[i];
            if (call_boundary) {
                cilk_spawn obase_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
            } else {
                cilk_spawn obase_bicut(t0, t1, l_son_grid, f);
            }

			l_son_grid.x0[i] = l_start + sep;
			l_son_grid.dx0[i] = slope_[i];
			l_son_grid.x1[i] = l_end;
			l_son_grid.dx1[i] = -slope_[i];
            if (call_boundary) {
                obase_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
            } else {
                obase_bicut(t0, t1, l_son_grid, f);
            }
			cilk_sync;

			l_son_grid.x0[i] = l_start + sep;
			l_son_grid.dx0[i] = -slope_[i];
			l_son_grid.x1[i] = l_start + sep;
			l_son_grid.dx1[i] = slope_[i];
            if (call_boundary) {
                cilk_spawn obase_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
            } else {
                cilk_spawn obase_bicut(t0, t1, l_son_grid, f);
            }

			if (l_start == phys_grid_.x0[i] && l_end == phys_grid_.x1[i]) {
        //        printf("merge triagles!\n");
				l_son_grid.x0[i] = l_end;
				l_son_grid.dx0[i] = -slope_[i];
				l_son_grid.x1[i] = l_end;
				l_son_grid.dx1[i] = slope_[i];
                if (call_boundary) {
                    cilk_spawn obase_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
                } else {
                    cilk_spawn obase_bicut(t0, t1, l_son_grid, f);
                }
			} else {
				if (l_father_grid.dx0[i] != slope_[i]) {
					l_son_grid.x0[i] = l_start; 
					l_son_grid.dx0[i] = l_father_grid.dx0[i];
					l_son_grid.x1[i] = l_start; 
					l_son_grid.dx1[i] = slope_[i];
                    if (call_boundary) {
                        cilk_spawn obase_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
                    } else {
                        cilk_spawn obase_bicut(t0, t1, l_son_grid, f);
                    }
				}
				if (l_father_grid.dx1[i] != -slope_[i]) {
					l_son_grid.x0[i] = l_end; 
					l_son_grid.dx0[i] = -slope_[i];
					l_son_grid.x1[i] = l_end; 
					l_son_grid.dx1[i] = l_father_grid.dx1[i];
                    if (call_boundary) {
                        cilk_spawn obase_bicut_boundary_p(t0, t1, l_son_grid, f, bf);
                    } else {
                        cilk_spawn obase_bicut(t0, t1, l_son_grid, f);
                    }
				}
			}
            return;
		}/* end if */
	} /* end for */    
    if (call_boundary)
        l_dt_stop = dt_recursive_boundary_;
    else
        l_dt_stop = dt_recursive_;

	if (lt > l_dt_stop) {
		int halflt = lt / 2;
		l_son_grid = l_father_grid;
        if (call_boundary) {
            obase_bicut_boundary_p(t0, t0+halflt, l_son_grid, f, bf);
        } else {
            obase_bicut(t0, t0+halflt, l_son_grid, f);
        }
		for (int i = 0; i < N_RANK; ++i) {
			l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
			l_son_grid.dx0[i] = l_father_grid.dx0[i];
			l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
			l_son_grid.dx1[i] = l_father_grid.dx1[i];
		}
        if (call_boundary) { 
            obase_bicut_boundary_p(t0+halflt, t1, l_son_grid, f, bf);
        } else {
            obase_bicut(t0+halflt, t1, l_son_grid, f);
        }
        return;
	}
	if (call_boundary) {
        /* for periodic stencils, all elements falling within boundary region
         * requires special treatment 'BF' (usually requires modulo operation)
        */
#if DEBUG
	    printf("call Boundary! ");
        print_grid(stdout, t0, t1, l_father_grid);
#endif
		//bf(t0, t1, grid);
		base_case_kernel_boundary(t0, t1, l_father_grid, bf);
    } else {
#if DEBUG
        printf("call Interior! ");
		print_grid(stdout, t0, t1, l_father_grid);
#endif
		f(t0, t1, l_father_grid);
    }
	return;
}

/* this is for optimizing base case!!! */
template <int N_RANK> template <typename F, typename BF>
inline void Algorithm<N_RANK>::obase_boundary_p(int t0, int t1, Grid_Info<N_RANK> const grid, F const & f, BF const & bf)
{
	/* cut into exact N_CORES pieces */
	/* Indirect memory access is expensive */
	int lt = t1 - t0;
	bool base_cube = (lt <= dt_recursive_); /* dt_recursive_ : temporal dimension stop */
	bool cut_yet = false, can_cut = false, call_boundary = false;
	index_info lb, thres;
    Grid_Info<N_RANK> l_father_grid = grid, l_son_grid;
    bool l_touch_boundary[N_RANK];

	for (int i = 0; i < N_RANK; ++i) {
        l_touch_boundary[i] = touch_boundary(i, lt, l_father_grid);
		lb[i] = (l_father_grid.x1[i] - l_father_grid.x0[i]);
		thres[i] = (initial_cut(i)) ?  N_CORES * (2 * slope_[i] * lt) : 2 * (2 * slope_[i] * lt);
		if (l_touch_boundary[i])
			base_cube = base_cube && (lb[i] <= dx_recursive_boundary_[i] || lb[i] < thres[i]); 
		else 
			base_cube = base_cube && (lb[i] <= dx_recursive_[i] || lb[i] < thres[i]); 
		call_boundary = call_boundary || l_touch_boundary[i];
	}	

	if (base_cube) {
		if (call_boundary) {
            /* for periodic stencils, all elements falling within boundary region
             * requires special treatment 'BF' (usually requires modulo operation)
            */
#if DEBUG
	        printf("call Boundary! ");
            print_grid(stdout, t0, t1, l_father_grid);
#endif
			//bf(t0, t1, grid);
			base_case_kernel_boundary(t0, t1, l_father_grid, bf);
        } else {
#if DEBUG
            printf("call Interior! ");
	    	print_grid(stdout, t0, t1, l_father_grid);
#endif
			f(t0, t1, l_father_grid);
        }
		return;
	} else  {
		for (int i = N_RANK-1; i >= 0 && !cut_yet; --i) {
			can_cut = (l_touch_boundary[i]) ? (lb[i] >= thres[i] && lb[i] > dx_recursive_boundary_[i]) : (lb[i] >= thres[i] && lb[i] > dx_recursive_[i]);
			if (can_cut) { 
                l_son_grid = l_father_grid;
                int sep = (initial_cut(i)) ? lb[i]/N_CORES : (2 * slope_[i] * lt);
                //int r = (initial_cut(i)) ? N_CORES : (lb[i]/sep);
                int r = lb[i]/sep;
				int l_start = (l_father_grid.x0[i]);
				int l_end = (l_father_grid.x1[i]);
				int j;
				for (j = 0; j < r-1; ++j) {
					l_son_grid.x0[i] = l_start + sep * j;
					l_son_grid.dx0[i] = slope_[i];
					l_son_grid.x1[i] = l_start + sep * (j+1);
					l_son_grid.dx1[i] = -slope_[i];
                    if (call_boundary) {
                        cilk_spawn obase_boundary_p(t0, t1, l_son_grid, f, bf);
                    } else {
                        cilk_spawn obase_adaptive(t0, t1, l_son_grid, f);
                    }
				}
				l_son_grid.x0[i] = l_start + sep * j;
				l_son_grid.dx0[i] = slope_[i];
				l_son_grid.x1[i] = l_end;
				l_son_grid.dx1[i] = -slope_[i];
                if (call_boundary) {
                    obase_boundary_p(t0, t1, l_son_grid, f, bf);
                } else {
                    obase_adaptive(t0, t1, l_son_grid, f);
                }
#if DEBUG
//				print_sync(stdout);
#endif
				cilk_sync;
				for (j = 1; j < r; ++j) {
					l_son_grid.x0[i] = l_start + sep * j;
					l_son_grid.dx0[i] = -slope_[i];
					l_son_grid.x1[i] = l_start + sep * j;
					l_son_grid.dx1[i] = slope_[i];
                    if (call_boundary) {
                        cilk_spawn obase_boundary_p(t0, t1, l_son_grid, f, bf);
                    } else {
                        cilk_spawn obase_adaptive(t0, t1, l_son_grid, f);
                    }
				}
				if (l_start == phys_grid_.x0[i] && l_end == phys_grid_.x1[i]) {
            //        printf("merge triagles!\n");
					l_son_grid.x0[i] = l_end;
					l_son_grid.dx0[i] = -slope_[i];
					l_son_grid.x1[i] = l_end;
					l_son_grid.dx1[i] = slope_[i];
                    if (call_boundary) {
                        cilk_spawn obase_boundary_p(t0, t1, l_son_grid, f, bf);
                    } else {
                        cilk_spawn obase_adaptive(t0, t1, l_son_grid, f);
                    }
				} else {
					if (l_father_grid.dx0[i] != slope_[i]) {
						l_son_grid.x0[i] = l_start; 
						l_son_grid.dx0[i] = l_father_grid.dx0[i];
						l_son_grid.x1[i] = l_start; 
						l_son_grid.dx1[i] = slope_[i];
                        if (call_boundary) {
                            cilk_spawn obase_boundary_p(t0, t1, l_son_grid, f, bf);
                        } else {
                            cilk_spawn obase_adaptive(t0, t1, l_son_grid, f);
                        }
					}
					if (l_father_grid.dx1[i] != -slope_[i]) {
						l_son_grid.x0[i] = l_end; 
						l_son_grid.dx0[i] = -slope_[i];
						l_son_grid.x1[i] = l_end; 
						l_son_grid.dx1[i] = l_father_grid.dx1[i];
                        if (call_boundary) {
                            cilk_spawn obase_boundary_p(t0, t1, l_son_grid, f, bf);
                        } else {
                            cilk_spawn obase_adaptive(t0, t1, l_son_grid, f);
                        }
					}
				}
				cut_yet = true;
			}/* end if */
		} /* end for */
		if (!cut_yet && lt > dt_recursive_) {
			int halflt = lt / 2;
			l_son_grid = l_father_grid;
            if (call_boundary) {
                obase_boundary_p(t0, t0+halflt, l_son_grid, f, bf);
            } else {
                obase_adaptive(t0, t0+halflt, l_son_grid, f);
            }
#if DEBUG
//			print_sync(stdout);
#endif
			for (int i = 0; i < N_RANK; ++i) {
				l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
				l_son_grid.dx0[i] = l_father_grid.dx0[i];
				l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
				l_son_grid.dx1[i] = l_father_grid.dx1[i];
			}
            if (call_boundary) { 
                obase_boundary_p(t0+halflt, t1, l_son_grid, f, bf);
            } else {
                obase_adaptive(t0+halflt, t1, l_son_grid, f);
            }
			cut_yet = true;
		}
		assert(cut_yet);
		return;
	}
}


#endif /* POCHOIR_WALK_RECURSIVE_HPP */