/*
  Copyright (c) 2017 Microsoft Corporation
  Author: Lev Nachmanson
*/
#include <string>
#include <algorithm>
#include <set>
#include "util/vector.h"
#include <utility>
#include "util/debug.h"
#include "util/lp/lu.h"
namespace lean {
#ifdef LEAN_DEBUG
template <typename T, typename X> // print the nr x nc submatrix at the top left corner
void print_submatrix(sparse_matrix<T, X> & m, unsigned mr, unsigned nc, std::ostream & out) {
    vector<vector<std::string>> A;
    vector<unsigned> widths;
    for (unsigned i = 0; i < m.row_count() && i < mr ; i++) {
        A.push_back(vector<std::string>());
        for (unsigned j = 0; j < m.column_count() && j < nc; j++) {
            A[i].push_back(T_to_string(static_cast<T>(m(i, j))));
        }
    }

    for (unsigned j = 0; j < m.column_count() && j < nc; j++) {
        widths.push_back(get_width_of_column(j, A));
    }

    print_matrix_with_widths(A, widths, out);
}

template<typename T, typename X>
void print_matrix(static_matrix<T, X> &m, std::ostream & out) {
    vector<vector<std::string>> A;
    vector<unsigned> widths;
    std::set<std::pair<unsigned, unsigned>> domain = m.get_domain();
    for (unsigned i = 0; i < m.row_count(); i++) {
        A.push_back(vector<std::string>());
        for (unsigned j = 0; j < m.column_count(); j++) {
            A[i].push_back(T_to_string(static_cast<T>(m(i, j))));
        }
    }

    for (unsigned j = 0; j < m.column_count(); j++) {
        widths.push_back(get_width_of_column(j, A));
    }

    print_matrix_with_widths(A, widths, out);
}

template <typename T, typename X>
void print_matrix(sparse_matrix<T, X>& m, std::ostream & out) {
    vector<vector<std::string>> A;
    vector<unsigned> widths;
    for (unsigned i = 0; i < m.row_count(); i++) {
        A.push_back(vector<std::string>());
        for (unsigned j = 0; j < m.column_count(); j++) {
            A[i].push_back(T_to_string(static_cast<T>(m(i, j))));
        }
    }

    for (unsigned j = 0; j < m.column_count(); j++) {
        widths.push_back(get_width_of_column(j, A));
    }

    print_matrix_with_widths(A, widths, out);
}
#endif


template <typename T, typename X>
one_elem_on_diag<T, X>::one_elem_on_diag(const one_elem_on_diag & o) {
    m_i = o.m_i;
    m_val = o.m_val;
#ifdef LEAN_DEBUG
    m_m = m_n = o.m_m;
    m_one_over_val = numeric_traits<T>::one() / o.m_val;
#endif
}

#ifdef LEAN_DEBUG
template <typename T, typename X>
T one_elem_on_diag<T, X>::get_elem(unsigned i, unsigned j) const {
    if (i == j){
        if (j == m_i) {
            return m_one_over_val;
        }
        return numeric_traits<T>::one();
    }

    return numeric_traits<T>::zero();
}
#endif
template <typename T, typename X>
void one_elem_on_diag<T, X>::apply_from_left_to_T(indexed_vector<T> & w, lp_settings & settings) {
    T & t = w[m_i];
    if (numeric_traits<T>::is_zero(t)) {
        return;
    }
    t /= m_val;
    if (numeric_traits<T>::precise()) return;
    if (settings.abs_val_is_smaller_than_drop_tolerance(t)) {
        w.erase_from_index(m_i);
        t = numeric_traits<T>::zero();
    }
}

// This class supports updates of the columns of B, and solves systems Bx=b,and yB=c
// Using Suhl-Suhl method described in the dissertation of Achim Koberstein, Chapter 5
template <typename T, typename X>
lu<T, X>::lu(static_matrix<T, X> const & A,
             vector<unsigned>& basis,
             lp_settings & settings):
    m_status(LU_status::OK),
    m_dim(A.row_count()),
    m_A(A),
    m_Q(m_dim),
    m_R(m_dim),
    m_r_wave(m_dim),
    m_U(A, basis), // create the square matrix that eventually will be factorized
    m_settings(settings),
    m_failure(false),
    m_row_eta_work_vector(A.row_count()),
    m_refactor_counter(0) {
    lean_assert(!(numeric_traits<T>::precise() && settings.use_tableau()));
#ifdef LEAN_DEBUG
    debug_test_of_basis(A, basis);
#endif
    ++m_settings.st().m_num_factorizations;
    create_initial_factorization();
#ifdef LEAN_DEBUG
    // lean_assert(check_correctness());
#endif
}
template <typename T, typename X>
void lu<T, X>::debug_test_of_basis(static_matrix<T, X> const & A, vector<unsigned> & basis) {
    std::set<unsigned> set;
    for (unsigned i = 0; i < A.row_count(); i++) {
        lean_assert(basis[i]< A.column_count());
        set.insert(basis[i]);
    }
    lean_assert(set.size() == A.row_count());
}

 template <typename T, typename X>
 void lu<T, X>::solve_By(indexed_vector<X> & y) {
     lean_assert(false); // not implemented
     // init_vector_y(y);
     // solve_By_when_y_is_ready(y);
 }


template <typename T, typename X>
void lu<T, X>::solve_By(vector<X> & y) {
    init_vector_y(y);
    solve_By_when_y_is_ready_for_X(y);
}

template <typename T, typename X>
void lu<T, X>::solve_By_when_y_is_ready_for_X(vector<X> & y) {
    if (numeric_traits<T>::precise()) {
        m_U.solve_U_y(y);
        m_R.apply_reverse_from_left_to_X(y); // see 24.3 from Chvatal
        return;
    }
    m_U.double_solve_U_y(y);
    m_R.apply_reverse_from_left_to_X(y); // see 24.3 from Chvatal
    unsigned i = m_dim;
    while (i--) {
        if (is_zero(y[i])) continue;
        if (m_settings.abs_val_is_smaller_than_drop_tolerance(y[i])){
            y[i] = zero_of_type<X>();
        }
    }
}

template <typename T, typename X>
void lu<T, X>::solve_By_when_y_is_ready_for_T(vector<T> & y, vector<unsigned> & index) {
    if (numeric_traits<T>::precise()) {
        m_U.solve_U_y(y);
        m_R.apply_reverse_from_left_to_T(y); // see 24.3 from Chvatal
        unsigned j = m_dim;
        while (j--) {
            if (!is_zero(y[j]))
                index.push_back(j);
        }
        return;
    }
    m_U.double_solve_U_y(y);
    m_R.apply_reverse_from_left_to_T(y); // see 24.3 from Chvatal
    unsigned i = m_dim;
    while (i--) {
        if (is_zero(y[i])) continue;
        if (m_settings.abs_val_is_smaller_than_drop_tolerance(y[i])){
            y[i] = zero_of_type<T>();
        } else {
            index.push_back(i);
        }
    }
}

template <typename T, typename X>
void lu<T, X>::solve_By_for_T_indexed_only(indexed_vector<T> & y, const lp_settings & settings) {
    if (numeric_traits<T>::precise()) {
        vector<unsigned> active_rows;
        m_U.solve_U_y_indexed_only(y, settings, active_rows);
        m_R.apply_reverse_from_left(y); // see 24.3 from Chvatal
        return;
    }
    m_U.double_solve_U_y(y, m_settings);
    m_R.apply_reverse_from_left(y); // see 24.3 from Chvatal
}

template <typename T, typename X>
void lu<T, X>::print_matrix_compact(std::ostream & f) {
    f << "matrix_start" << std::endl;
    f << "nrows " << m_A.row_count() << std::endl;
    f << "ncolumns " << m_A.column_count() << std::endl;
    for (unsigned i = 0; i < m_A.row_count(); i++) {
        auto & row = m_A.m_rows[i];
        f << "row " << i << std::endl;
        for (auto & t : row) {
            f << "column " << t.m_j << " value " << t.m_value << std::endl;
        }
        f << "row_end" << std::endl;
    }
    f << "matrix_end" << std::endl;
}
template <typename T, typename X>
void lu<T, X>::print(indexed_vector<T> & w, const vector<unsigned>& basis) {
    std::string dump_file_name("/tmp/lu");
    remove(dump_file_name.c_str());
    std::ofstream f(dump_file_name);
    if (!f.is_open()) {
        LP_OUT(m_settings, "cannot open file " << dump_file_name << std::endl);
        return;
    }
    LP_OUT(m_settings, "writing lu dump to " << dump_file_name << std::endl);
    print_matrix_compact(f);
    print_vector(basis, f);
    print_indexed_vector(w, f);
    f.close();
}
template <typename T, typename X>
void lu<T, X>::solve_Bd(unsigned a_column, indexed_vector<T> & d, indexed_vector<T> & w) {
    init_vector_w(a_column, w);

    if (w.m_index.size() * ratio_of_index_size_to_all_size<T>() < d.m_data.size()) { // this const might need some tuning
        d = w;
        solve_By_for_T_indexed_only(d, m_settings);
    } else {
        d.m_data = w.m_data;
        d.m_index.clear();
        solve_By_when_y_is_ready_for_T(d.m_data, d.m_index);
    }
}

template <typename T, typename X>
void lu<T, X>::solve_Bd_faster(unsigned a_column, indexed_vector<T> & d) { // puts the a_column into d
    init_vector_w(a_column, d);
    solve_By_for_T_indexed_only(d, m_settings);
}

template <typename T, typename X>
void lu<T, X>::solve_yB(vector<T>& y) {
    // first solve yU = cb*R(-1)
    m_R.apply_reverse_from_right_to_T(y); // got y = cb*R(-1)
    m_U.solve_y_U(y); // got y*U=cb*R(-1)
    m_Q.apply_reverse_from_right_to_T(y); //
    for (auto e = m_tail.rbegin(); e != m_tail.rend(); ++e) {
#ifdef LEAN_DEBUG
        (*e)->set_number_of_columns(m_dim);
#endif
        (*e)->apply_from_right(y);
    }
}

template <typename T, typename X>
void lu<T, X>::solve_yB_indexed(indexed_vector<T>& y) {
    lean_assert(y.is_OK());
    // first solve yU = cb*R(-1)
    m_R.apply_reverse_from_right_to_T(y); // got y = cb*R(-1)
    lean_assert(y.is_OK());
    m_U.solve_y_U_indexed(y, m_settings); // got y*U=cb*R(-1)
    lean_assert(y.is_OK());
    m_Q.apply_reverse_from_right_to_T(y);
    lean_assert(y.is_OK());
    for (auto e = m_tail.rbegin(); e != m_tail.rend(); ++e) {
#ifdef LEAN_DEBUG
        (*e)->set_number_of_columns(m_dim);
#endif
        (*e)->apply_from_right(y);
        lean_assert(y.is_OK());
    }
}

template <typename T, typename X>
void lu<T, X>::add_delta_to_solution(const vector<T>& yc, vector<T>& y){
    unsigned i = static_cast<unsigned>(y.size());
    while (i--)
        y[i]+=yc[i];
}

template <typename T, typename X>
void lu<T, X>::add_delta_to_solution_indexed(indexed_vector<T>& y) {
    // the delta sits in m_y_copy, put result into y
    lean_assert(y.is_OK());
    lean_assert(m_y_copy.is_OK());
    m_ii.clear();
    m_ii.resize(y.data_size());
    for (unsigned i : y.m_index)
        m_ii.set_value(1, i);
    for (unsigned i : m_y_copy.m_index) {
        y.m_data[i] += m_y_copy[i];
        if (m_ii[i] == 0)
            m_ii.set_value(1, i);
    }
    lean_assert(m_ii.is_OK());
    y.m_index.clear();

    for (unsigned i : m_ii.m_index) {
        T & v = y.m_data[i];
        if (!lp_settings::is_eps_small_general(v, 1e-14))
            y.m_index.push_back(i);
        else if (!numeric_traits<T>::is_zero(v))
            v = zero_of_type<T>();
    }
        
    lean_assert(y.is_OK());
}

template <typename T, typename X>
void lu<T, X>::find_error_of_yB(vector<T>& yc, const vector<T>& y, const vector<unsigned>& m_basis) {
    unsigned i = m_dim;
    while (i--) {
        yc[i] -= m_A.dot_product_with_column(y, m_basis[i]);
    }
}

template <typename T, typename X>
void lu<T, X>::find_error_of_yB_indexed(const indexed_vector<T>& y, const vector<int>& heading, const lp_settings& settings) {
#if 0 == 1
    // it is a non efficient version
    indexed_vector<T> yc = m_y_copy;
    yc.m_index.clear();
    lean_assert(!numeric_traits<T>::precise());
    {

        vector<unsigned> d_basis(y.m_data.size());
        for (unsigned j = 0; j < heading.size(); j++) {
            if (heading[j] >= 0) {
                d_basis[heading[j]] = j;
            }
        }
        
        
        unsigned i = m_dim;
        while (i--) {
            T & v = yc.m_data[i] -= m_A.dot_product_with_column(y.m_data, d_basis[i]);
            if (settings.abs_val_is_smaller_than_drop_tolerance(v))
                v = zero_of_type<T>();
            else
                yc.m_index.push_back(i);
        }
    }
#endif
    lean_assert(m_ii.is_OK());
    m_ii.clear();
    m_ii.resize(y.data_size());
    lean_assert(m_y_copy.is_OK());
    // put the error into m_y_copy
    for (auto k : y.m_index) {
        auto & row = m_A.m_rows[k];
        const T & y_k = y.m_data[k];
        for (auto & c : row) {
            unsigned j = c.m_j;
            int hj = heading[j];
            if (hj < 0) continue;
            if (m_ii.m_data[hj] == 0)
                m_ii.set_value(1, hj);
            m_y_copy.m_data[hj] -= c.get_val() * y_k;
        }
    }
    // add the index of m_y_copy to m_ii
    for (unsigned i : m_y_copy.m_index) {
        if (m_ii.m_data[i] == 0)
            m_ii.set_value(1, i);
    }
    
    // there is no guarantee that m_y_copy is OK here, but its index
    // is contained in m_ii index
    m_y_copy.m_index.clear();
    // setup the index of m_y_copy
    for (auto k : m_ii.m_index) {
        T& v = m_y_copy.m_data[k];
        if (settings.abs_val_is_smaller_than_drop_tolerance(v))
            v = zero_of_type<T>();
        else {
            m_y_copy.set_value(v, k);
        }
    }
    lean_assert(m_y_copy.is_OK());

}




// solves y*B = y
// y is the input
template <typename T, typename X>
void lu<T, X>::solve_yB_with_error_check_indexed(indexed_vector<T> & y, const vector<int>& heading,  const vector<unsigned> & basis, const lp_settings & settings) {
    if (numeric_traits<T>::precise()) {
        if (y.m_index.size() * ratio_of_index_size_to_all_size<T>() * 3 < m_A.column_count()) {
            solve_yB_indexed(y);
        } else {
            solve_yB(y.m_data);
            y.restore_index_and_clean_from_data();
        }
        return;
    }
    lean_assert(m_y_copy.is_OK());
    lean_assert(y.is_OK());
    if (y.m_index.size() * ratio_of_index_size_to_all_size<T>() < m_A.column_count()) {
        m_y_copy = y;
        solve_yB_indexed(y);
        lean_assert(y.is_OK());
        if (y.m_index.size() * ratio_of_index_size_to_all_size<T>() >= m_A.column_count()) {
            find_error_of_yB(m_y_copy.m_data, y.m_data, basis);
            solve_yB(m_y_copy.m_data);
            add_delta_to_solution(m_y_copy.m_data, y.m_data);
            y.restore_index_and_clean_from_data();
            m_y_copy.clear_all();
        } else {
            find_error_of_yB_indexed(y, heading, settings); // this works with m_y_copy
            solve_yB_indexed(m_y_copy);
            add_delta_to_solution_indexed(y);
        }
        lean_assert(m_y_copy.is_OK());
    } else {
        solve_yB_with_error_check(y.m_data, basis);
        y.restore_index_and_clean_from_data();
    }
}


// solves y*B = y
// y is the input
template <typename T, typename X>
void lu<T, X>::solve_yB_with_error_check(vector<T> & y, const vector<unsigned>& basis) {
    if (numeric_traits<T>::precise()) {
        solve_yB(y);
        return;
    }
    auto & yc = m_y_copy.m_data;
    yc =y; // copy y aside
    solve_yB(y);
    find_error_of_yB(yc, y, basis);
    solve_yB(yc);
    add_delta_to_solution(yc, y);
    m_y_copy.clear_all();
}
template <typename T, typename X>
void lu<T, X>::apply_Q_R_to_U(permutation_matrix<T, X> & r_wave) {
    m_U.multiply_from_right(r_wave);
    m_U.multiply_from_left_with_reverse(r_wave);
}


// Solving yB = cb to find the entering variable,
// where cb is the cost vector projected to B.
// The result is stored in cb.

// solving Bd = a ( to find the column d of B^{-1} A_N corresponding to the entering
// variable
template <typename T, typename X>
lu<T, X>::~lu(){
    for (auto t : m_tail) {
        delete t;
    }
}
template <typename T, typename X>
void lu<T, X>::init_vector_y(vector<X> & y) {
    apply_lp_list_to_y(y);
    m_Q.apply_reverse_from_left_to_X(y);
}

template <typename T, typename X>
void lu<T, X>::perform_transformations_on_w(indexed_vector<T>& w) {
    apply_lp_list_to_w(w);
    m_Q.apply_reverse_from_left(w);
    // TBD does not compile: lean_assert(numeric_traits<T>::precise() || check_vector_for_small_values(w, m_settings));
}

// see Chvatal 24.3
template <typename T, typename X>
void lu<T, X>::init_vector_w(unsigned entering, indexed_vector<T> & w) {
    w.clear();
    m_A.copy_column_to_indexed_vector(entering, w); // w = a, the column
    perform_transformations_on_w(w);
}
template <typename T, typename X>
void lu<T, X>::apply_lp_list_to_w(indexed_vector<T> & w) {
    for (unsigned i = 0; i < m_tail.size(); i++) {
        m_tail[i]->apply_from_left_to_T(w, m_settings);
        // TBD does not compile: lean_assert(check_vector_for_small_values(w, m_settings));
    }
}
template <typename T, typename X>
void lu<T, X>::apply_lp_list_to_y(vector<X>& y) {
    for (unsigned i = 0; i < m_tail.size(); i++) {
        m_tail[i]->apply_from_left(y, m_settings);
    }
}
template <typename T, typename X>
void lu<T, X>::swap_rows(int j, int k) {
    if (j != k) {
        m_Q.transpose_from_left(j, k);
        m_U.swap_rows(j, k);
    }
}

template <typename T, typename X>
void lu<T, X>::swap_columns(int j, int pivot_column) {
    if (j == pivot_column)
        return;
    m_R.transpose_from_right(j, pivot_column);
    m_U.swap_columns(j, pivot_column);
}
template <typename T, typename X>
bool lu<T, X>::pivot_the_row(int row) {
    eta_matrix<T, X> * eta_matrix = get_eta_matrix_for_pivot(row);
    if (get_status() != LU_status::OK) {
        return false;
    }

    if (eta_matrix == nullptr) {
        m_U.shorten_active_matrix(row, nullptr);
        return true;
    }
    if (!m_U.pivot_with_eta(row, eta_matrix, m_settings))
        return false;
    eta_matrix->conjugate_by_permutation(m_Q);
    push_matrix_to_tail(eta_matrix);
    return true;
}
// we're processing the column j now
template <typename T, typename X>
eta_matrix<T, X> * lu<T, X>::get_eta_matrix_for_pivot(unsigned j) {
    eta_matrix<T, X> *ret;
    if(!m_U.fill_eta_matrix(j, &ret)) {
        set_status(LU_status::Degenerated);
    }
    return ret;
}
// we're processing the column j now
template <typename T, typename X>
eta_matrix<T, X> * lu<T, X>::get_eta_matrix_for_pivot(unsigned j, sparse_matrix<T, X>& copy_of_U) {
    eta_matrix<T, X> *ret;
    copy_of_U.fill_eta_matrix(j, &ret);
    return ret;
}

// see page 407 of Chvatal
template <typename T, typename X>
unsigned lu<T, X>::transform_U_to_V_by_replacing_column(indexed_vector<T> & w,
                                                        unsigned leaving_column) {
    unsigned column_to_replace = m_R.apply_reverse(leaving_column);
    m_U.replace_column(column_to_replace, w, m_settings);
    return column_to_replace;
}

#ifdef LEAN_DEBUG
template <typename T, typename X>
void lu<T, X>::check_vector_w(unsigned entering) {
    T * w = new T[m_dim];
    m_A.copy_column_to_vector(entering, w);
    check_apply_lp_lists_to_w(w);
    delete [] w;
}
template <typename T, typename X>
void lu<T, X>::check_apply_matrix_to_vector(matrix<T, X> *lp, T *w) {
    if (lp != nullptr) {
        lp -> set_number_of_rows(m_dim);
        lp -> set_number_of_columns(m_dim);
        apply_to_vector(*lp, w);
    }
}

template <typename T, typename X>
void lu<T, X>::check_apply_lp_lists_to_w(T * w) {
    for (unsigned i = 0; i < m_tail.size(); i++) {
        check_apply_matrix_to_vector(m_tail[i], w);
    }
    permutation_matrix<T, X> qr = m_Q.get_reverse();
    apply_to_vector(qr, w);
    for (int i = m_dim - 1; i >= 0; i--) {
        lean_assert(abs(w[i] - w[i]) < 0.0000001);
    }
}

#endif
template <typename T, typename X>
void lu<T, X>::process_column(int j) {
    unsigned pi, pj;
    bool success = m_U.get_pivot_for_column(pi, pj, m_settings.c_partial_pivoting, j);
    if (!success) {
        LP_OUT(m_settings, "get_pivot returned false: cannot find the pivot for column " << j << std::endl);
        m_failure = true;
        return;
    }

    if (static_cast<int>(pi) == -1) {
        LP_OUT(m_settings, "cannot find the pivot for column " << j << std::endl);
        m_failure = true;
        return;
    }
    swap_columns(j, pj);
    swap_rows(j, pi);
    if (!pivot_the_row(j)) {
        //      LP_OUT(m_settings, "pivot_the_row(" << j << ") failed" << std::endl);
        m_failure = true;
    }
}
template <typename T, typename X>
bool lu<T, X>::is_correct(const vector<unsigned>& basis) {
#ifdef LEAN_DEBUG
    if (get_status() != LU_status::OK) {
        return false;
    }
    dense_matrix<T, X> left_side = get_left_side(basis);
    dense_matrix<T, X> right_side = get_right_side();
    return left_side == right_side;
#else
    return true;
#endif
}


#ifdef LEAN_DEBUG
template <typename T, typename X>
dense_matrix<T, X> lu<T, X>::tail_product() {
    lean_assert(tail_size() > 0);
    dense_matrix<T, X> left_side = permutation_matrix<T, X>(m_dim);
    for (unsigned i = 0; i < tail_size(); i++) {
        matrix<T, X>* lp =  get_lp_matrix(i);
        lp->set_number_of_rows(m_dim);
        lp->set_number_of_columns(m_dim);
        left_side = ((*lp) * left_side);
    }
    return left_side;
}
template <typename T, typename X>
dense_matrix<T, X> lu<T, X>::get_left_side(const vector<unsigned>& basis) {
    dense_matrix<T, X> left_side = get_B(*this, basis);
    for (unsigned i = 0; i < tail_size(); i++) {
        matrix<T, X>* lp =  get_lp_matrix(i);
        lp->set_number_of_rows(m_dim);
        lp->set_number_of_columns(m_dim);
        left_side = ((*lp) * left_side);
    }
    return left_side;
}
template <typename T, typename X>
dense_matrix<T, X>  lu<T, X>::get_right_side() {
    auto ret = U() * R();
    ret = Q() * ret;
    return ret;
}
#endif

// needed for debugging purposes
template <typename T, typename X>
void lu<T, X>::copy_w(T *buffer, indexed_vector<T> & w) {
    unsigned i = m_dim;
    while (i--) {
        buffer[i] = w[i];
    }
}

// needed for debugging purposes
template <typename T, typename X>
void lu<T, X>::restore_w(T *buffer, indexed_vector<T> & w) {
    unsigned i = m_dim;
    while (i--) {
        w[i] = buffer[i];
    }
}
template <typename T, typename X>
bool lu<T, X>::all_columns_and_rows_are_active() {
    unsigned i = m_dim;
    while (i--) {
        lean_assert(m_U.col_is_active(i));
        lean_assert(m_U.row_is_active(i));
    }
    return true;
}
template <typename T, typename X>
bool lu<T, X>::too_dense(unsigned j) const {
    unsigned r = m_dim - j;
    if (r < 5)
        return false;
     // if (j * 5 < m_dim * 4) // start looking for dense only at the bottom  of the rows
     //    return false;
    //    return r * r * m_settings.density_threshold <= m_U.get_number_of_nonzeroes_below_row(j);
    return r * r * m_settings.density_threshold <= m_U.get_n_of_active_elems();
}
template <typename T, typename X>
void lu<T, X>::pivot_in_dense_mode(unsigned i) {
    int j = m_dense_LU->find_pivot_column_in_row(i);
    if (j == -1) {
        m_failure = true;
        return;
    }
    if (i != static_cast<unsigned>(j)) {
        swap_columns(i, j);
        m_dense_LU->swap_columns(i, j);
    }
    m_dense_LU->pivot(i, m_settings);
}
template <typename T, typename X>
void lu<T, X>::create_initial_factorization(){
    m_U.prepare_for_factorization();
    unsigned j;
    for (j = 0; j < m_dim; j++) {
        process_column(j);
        if (m_failure) {
            set_status(LU_status::Degenerated);
            return;
        }
        if (too_dense(j)) {
            break;
        }
    }
    if (j == m_dim) {
        // TBD does not compile: lean_assert(m_U.is_upper_triangular_and_maximums_are_set_correctly_in_rows(m_settings));
        //        lean_assert(is_correct());
        // lean_assert(m_U.is_upper_triangular_and_maximums_are_set_correctly_in_rows(m_settings));
        return;
    }
    j++;
    m_dense_LU = new square_dense_submatrix<T, X>(&m_U, j);
    for (; j < m_dim; j++) {
        pivot_in_dense_mode(j);
        if (m_failure) {
            set_status(LU_status::Degenerated);
            return;
        }
    }
    m_dense_LU->update_parent_matrix(m_settings);
    lean_assert(m_dense_LU->is_L_matrix());
    m_dense_LU->conjugate_by_permutation(m_Q);
    push_matrix_to_tail(m_dense_LU);
    m_refactor_counter = 0;
    // lean_assert(is_correct());
    // lean_assert(m_U.is_upper_triangular_and_maximums_are_set_correctly_in_rows(m_settings));
}

template <typename T, typename X>
void lu<T, X>::calculate_r_wave_and_update_U(unsigned bump_start, unsigned bump_end, permutation_matrix<T, X> & r_wave) {
    if (bump_start > bump_end) {
        set_status(LU_status::Degenerated);
        return;
    }
    if (bump_start == bump_end) {
        return;
    }

    r_wave[bump_start] = bump_end; // sending the offensive column to the end of the bump

    for ( unsigned i = bump_start + 1 ; i <= bump_end; i++ ) {
        r_wave[i] = i - 1;
    }

    m_U.multiply_from_right(r_wave);
    m_U.multiply_from_left_with_reverse(r_wave);
}
template <typename T, typename X>
void lu<T, X>::scan_last_row_to_work_vector(unsigned lowest_row_of_the_bump) {
    vector<indexed_value<T>> & last_row_vec = m_U.get_row_values(m_U.adjust_row(lowest_row_of_the_bump));
    for (auto & iv : last_row_vec) {
        if (is_zero(iv.m_value)) continue;
        lean_assert(!m_settings.abs_val_is_smaller_than_drop_tolerance(iv.m_value));
        unsigned adjusted_col = m_U.adjust_column_inverse(iv.m_index);
        if (adjusted_col < lowest_row_of_the_bump) {
            m_row_eta_work_vector.set_value(-iv.m_value, adjusted_col);
        } else  {
            m_row_eta_work_vector.set_value(iv.m_value, adjusted_col); // preparing to calculate the real value in the matrix
        }
    }
}

template <typename T, typename X>
void lu<T, X>::pivot_and_solve_the_system(unsigned replaced_column, unsigned lowest_row_of_the_bump) {
    // we have the system right side at m_row_eta_work_vector now
    // solve the system column wise
    for (unsigned j = replaced_column; j < lowest_row_of_the_bump; j++) {
        T v = m_row_eta_work_vector[j];
        if (numeric_traits<T>::is_zero(v)) continue; // this column does not contribute to the solution
        unsigned aj = m_U.adjust_row(j);
        vector<indexed_value<T>> & row = m_U.get_row_values(aj);
        for (auto & iv : row) {
            unsigned col = m_U.adjust_column_inverse(iv.m_index);
            lean_assert(col >= j || numeric_traits<T>::is_zero(iv.m_value));
            if (col == j) continue;
            if (numeric_traits<T>::is_zero(iv.m_value)) {
                continue;
            }
            // the -v is for solving the system ( to zero the last row), and +v is for pivoting
            T delta = col < lowest_row_of_the_bump? -v * iv.m_value: v * iv.m_value;
            lean_assert(numeric_traits<T>::is_zero(delta) == false);


            
           // m_row_eta_work_vector.add_value_at_index_with_drop_tolerance(col, delta);
            if (numeric_traits<T>::is_zero(m_row_eta_work_vector[col])) {
                if (!m_settings.abs_val_is_smaller_than_drop_tolerance(delta)){
                    m_row_eta_work_vector.set_value(delta, col);
                }
            } else {
                T t = (m_row_eta_work_vector[col] += delta);
                if (m_settings.abs_val_is_smaller_than_drop_tolerance(t)){
                    m_row_eta_work_vector[col] = numeric_traits<T>::zero();
                    auto it = std::find(m_row_eta_work_vector.m_index.begin(), m_row_eta_work_vector.m_index.end(), col);
                    if (it != m_row_eta_work_vector.m_index.end())
                        m_row_eta_work_vector.m_index.erase(it);
                }
                }
        }
    }
}
// see Achim Koberstein's thesis page 58, but here we solve the system and pivot to the last
// row at the same time
template <typename T, typename X>
row_eta_matrix<T, X> *lu<T, X>::get_row_eta_matrix_and_set_row_vector(unsigned replaced_column, unsigned lowest_row_of_the_bump, const T &  pivot_elem_for_checking) {
    if (replaced_column == lowest_row_of_the_bump) return nullptr;
    scan_last_row_to_work_vector(lowest_row_of_the_bump);
    pivot_and_solve_the_system(replaced_column, lowest_row_of_the_bump);
    if (numeric_traits<T>::precise() == false && !is_zero(pivot_elem_for_checking)) {
        T denom = std::max(T(1), abs(pivot_elem_for_checking));
        if (
            !m_settings.abs_val_is_smaller_than_pivot_tolerance((m_row_eta_work_vector[lowest_row_of_the_bump] - pivot_elem_for_checking) / denom)) {
            set_status(LU_status::Degenerated);
            //        LP_OUT(m_settings, "diagonal element is off" << std::endl);
            return nullptr;
        }
    }
#ifdef LEAN_DEBUG
    auto ret = new row_eta_matrix<T, X>(replaced_column, lowest_row_of_the_bump, m_dim);
#else
    auto ret = new row_eta_matrix<T, X>(replaced_column, lowest_row_of_the_bump);
#endif

    for (auto j : m_row_eta_work_vector.m_index) {
        if (j < lowest_row_of_the_bump) {
            auto & v = m_row_eta_work_vector[j];
            if (!is_zero(v)) {
                if (!m_settings.abs_val_is_smaller_than_drop_tolerance(v)){
                    ret->push_back(j, v);
                }
                v = numeric_traits<T>::zero();
            }
        }
    } // now the lowest_row_of_the_bump contains the rest of the row to the right of the bump with correct values
    return ret;
}

template <typename T, typename X>
void lu<T, X>::replace_column(T pivot_elem_for_checking, indexed_vector<T> & w, unsigned leaving_column_of_U){
    m_refactor_counter++;
    unsigned replaced_column =  transform_U_to_V_by_replacing_column( w, leaving_column_of_U);
    unsigned lowest_row_of_the_bump = m_U.lowest_row_in_column(replaced_column);
    m_r_wave.init(m_dim);
    calculate_r_wave_and_update_U(replaced_column, lowest_row_of_the_bump, m_r_wave);
    auto row_eta = get_row_eta_matrix_and_set_row_vector(replaced_column, lowest_row_of_the_bump, pivot_elem_for_checking);

    if (get_status() == LU_status::Degenerated) {
        m_row_eta_work_vector.clear_all();
        return;
    }
    m_Q.multiply_by_permutation_from_right(m_r_wave);
    m_R.multiply_by_permutation_reverse_from_left(m_r_wave);
    if (row_eta != nullptr) {
        row_eta->conjugate_by_permutation(m_Q);
        push_matrix_to_tail(row_eta);
    }
    calculate_Lwave_Pwave_for_bump(replaced_column, lowest_row_of_the_bump);
    // lean_assert(m_U.is_upper_triangular_and_maximums_are_set_correctly_in_rows(m_settings));
    // lean_assert(w.is_OK() && m_row_eta_work_vector.is_OK());
}
template <typename T, typename X>
void lu<T, X>::calculate_Lwave_Pwave_for_bump(unsigned replaced_column, unsigned lowest_row_of_the_bump){
    T diagonal_elem;
    if (replaced_column < lowest_row_of_the_bump) {
        diagonal_elem = m_row_eta_work_vector[lowest_row_of_the_bump];
        //          lean_assert(m_row_eta_work_vector.is_OK());
        m_U.set_row_from_work_vector_and_clean_work_vector_not_adjusted(m_U.adjust_row(lowest_row_of_the_bump), m_row_eta_work_vector, m_settings);
    } else {
        diagonal_elem = m_U(lowest_row_of_the_bump, lowest_row_of_the_bump); // todo - get it more efficiently
    }
    if (m_settings.abs_val_is_smaller_than_pivot_tolerance(diagonal_elem)) {
        set_status(LU_status::Degenerated);
        return;
    }

    calculate_Lwave_Pwave_for_last_row(lowest_row_of_the_bump, diagonal_elem);
    //         lean_assert(m_U.is_upper_triangular_and_maximums_are_set_correctly_in_rows(m_settings));
}

template <typename T, typename X>
void lu<T, X>::calculate_Lwave_Pwave_for_last_row(unsigned lowest_row_of_the_bump, T diagonal_element) {
    auto l = new one_elem_on_diag<T, X>(lowest_row_of_the_bump, diagonal_element);
#ifdef LEAN_DEBUG
    l->set_number_of_columns(m_dim);
#endif
    push_matrix_to_tail(l);
    m_U.divide_row_by_constant(lowest_row_of_the_bump, diagonal_element, m_settings);
    l->conjugate_by_permutation(m_Q);
}

template <typename T, typename X>
void init_factorization(lu<T, X>* & factorization, static_matrix<T, X> & m_A, vector<unsigned> & m_basis, lp_settings &m_settings) {
    if (factorization != nullptr)
        delete factorization;
    factorization = new lu<T, X>(m_A, m_basis, m_settings);
    // if (factorization->get_status() != LU_status::OK) 
    //     LP_OUT(m_settings, "failing in init_factorization" << std::endl);
}

#ifdef LEAN_DEBUG
template <typename T, typename X>
dense_matrix<T, X>  get_B(lu<T, X>& f, const vector<unsigned>& basis) {
    lean_assert(basis.size() == f.dimension());
    lean_assert(basis.size() == f.m_U.dimension());
    dense_matrix<T, X>  B(f.dimension(), f.dimension());
    for (unsigned i = 0; i < f.dimension(); i++)
        for (unsigned j = 0; j < f.dimension(); j++)
            B.set_elem(i, j, f.B_(i, j, basis));

    return B;
}
#endif
}
