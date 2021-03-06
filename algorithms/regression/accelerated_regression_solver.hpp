#ifndef SKYLARK_ACCELERATED_REGRESSION_SOLVER_HPP
#define SKYLARK_ACCELERATED_REGRESSION_SOLVER_HPP

#include "config.h"

namespace skylark {
namespace algorithms {

/**
 * Regression solver on the original problem that have been accelerated using
 * sketching. Note that we aim to solve the problem exactly (as possible on
 * a machine) and not approximately (as in sketched_regression_solver_t).
 *
 * A regression solver accepts a right-hand side and output a solution
 * the the regression problem.
 *
 * The regression problem is fixed, so it is a parameter of the function
 * constructing the regressor. The top class is empty: real logic is in
 * specializations.
 *
 * @tparam RegressionProblemType Type of regression problem solved.
 * @tparam RhsType Right-hand side matrix type.
 * @tparam SolType Solution matrix type.
 * @tparam AlgTag Tag specifying the algorithm used (tags differ based on
 *                problem).
 */
template <typename RegressionProblemType,
          typename RhsType,
          typename SolType,
          typename AlgTag>
class accelerated_regression_solver_t {

};


} // namespace algorithms
} // namespace skylark


#include "accelerated_linearl2_regression_solver.hpp"

#endif // SKYLARK_ACCELERATED_REGRESSION_SOLVER_HPP
