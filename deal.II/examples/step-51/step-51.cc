/* ---------------------------------------------------------------------
 * $Id$
 *
 * Copyright (C) 2013 - 2013 by the deal.II authors
 *
 * This file is part of the deal.II library.
 *
 * The deal.II library is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE at
 * the top level of the deal.II distribution.
 *
 * ---------------------------------------------------------------------

 *
 * Author: Martin Kronbichler, TU Muenchen,
 *         Scott T. Miller, xxx, 2013
 */


#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/function.h>
#include <deal.II/base/tensor_function.h>
#include <deal.II/base/exceptions.h>
#include <deal.II/base/logstream.h>
#include <deal.II/base/convergence_table.h>
#include <deal.II/lac/vector.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/compressed_simple_sparsity_pattern.h>
#include <deal.II/lac/solver_gmres.h>
#include <deal.II/lac/precondition.h>
#include <deal.II/grid/tria.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/grid/tria_boundary_lib.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/data_out.h>

#include <deal.II/fe/fe_dgq.h>
#include <deal.II/fe/fe_face.h>

#include <deal.II/lac/chunk_sparse_matrix.h>
#include <deal.II/numerics/data_out_faces.h>

namespace Step51
{
  using namespace dealii;

  // @sect3{Equation data}

  // The structure of the analytic solution is the same as in step-7. There
  // are two exceptions. Firstly, we also create a solution for the 3d case,
  // and secondly, we take into account the convection velocity in the right
  // hand side that is variable in this case.
  template <int dim>
  class SolutionBase
  {
  protected:
    static const unsigned int  n_source_centers = 3;
    static const Point<dim>    source_centers[n_source_centers];
    static const double        width;
  };


  template <>
  const Point<1>
  SolutionBase<1>::source_centers[SolutionBase<1>::n_source_centers]
    = { Point<1>(-1.0 / 3.0),
        Point<1>(0.0),
        Point<1>(+1.0 / 3.0)
      };


  template <>
  const Point<2>
  SolutionBase<2>::source_centers[SolutionBase<2>::n_source_centers]
    = { Point<2>(-0.5, +0.5),
        Point<2>(-0.5, -0.5),
        Point<2>(+0.5, -0.5)
      };

  template <>
  const Point<3>
  SolutionBase<3>::source_centers[SolutionBase<3>::n_source_centers]
  = { Point<3>(-0.5, +0.5, 0.25),
      Point<3>(-0.6, -0.5, -0.125),
      Point<3>(+0.5, -0.5, 0.5)   };

  template <int dim>
  const double SolutionBase<dim>::width = 1./5.;



  template <int dim>
  class ConvectionVelocity : public TensorFunction<1,dim>
  {
  public:
    ConvectionVelocity() : TensorFunction<1,dim>() {}

    virtual Tensor<1,dim> value (const Point<dim> &p) const;
  };



  template <int dim>
  Tensor<1,dim>
  ConvectionVelocity<dim>::value(const Point<dim> &p) const
  {
    Tensor<1,dim> convection;
    switch (dim)
      {
      case 1:
        convection[0] = 1;
        break;
      case 2:
        convection[0] = p[1];
        convection[1] = -p[0];
        break;
      case 3:
        convection[0] = p[1];
        convection[1] = -p[0];
        convection[2] = 1;
        break;
      default:
        Assert(false, ExcNotImplemented());
      }
    return convection;
  }


  template <int dim>
  class Solution : public Function<dim>,
                   protected SolutionBase<dim>
  {
  public:
    Solution () : Function<dim>() {}

    virtual double value (const Point<dim>   &p,
			  const unsigned int  component = 0) const;

    virtual Tensor<1,dim> gradient (const Point<dim>   &p,
				    const unsigned int  component = 0) const;
  };



  template <int dim>
  double Solution<dim>::value (const Point<dim>   &p,
                               const unsigned int) const
  {
    double return_value = 0;
    for (unsigned int i=0; i<this->n_source_centers; ++i)
      {
        const Point<dim> x_minus_xi = p - this->source_centers[i];
        return_value += std::exp(-x_minus_xi.square() /
                                 (this->width * this->width));
      }

    return return_value /
      Utilities::fixed_power<dim>(std::sqrt(2. * numbers::PI) * this->width);
  }



  template <int dim>
  Tensor<1,dim> Solution<dim>::gradient (const Point<dim>   &p,
                                         const unsigned int) const
  {
    Tensor<1,dim> return_value;

    for (unsigned int i=0; i<this->n_source_centers; ++i)
      {
        const Point<dim> x_minus_xi = p - this->source_centers[i];

        return_value += (-2 / (this->width * this->width) *
                         std::exp(-x_minus_xi.square() /
                                  (this->width * this->width)) *
                         x_minus_xi);
      }

    return return_value / Utilities::fixed_power<dim>(std::sqrt(2 * numbers::PI) *
                                                      this->width);
  }



  template <int dim>
  class SolutionAndGradient : public Function<dim>,
                              protected SolutionBase<dim>
  {
  public:
    SolutionAndGradient () : Function<dim>(dim) {}

    virtual void vector_value (const Point<dim>   &p,
                               Vector<double>     &v) const
    {
      AssertDimension(v.size(), dim+1);
      Solution<dim> solution;
      Tensor<1,dim> grad = solution.gradient(p);
      for (unsigned int d=0; d<dim; ++d)
        v[d] = -grad[d];
      v[dim] = solution.value(p);
    }
  };



  template <int dim>
  class RightHandSide : public Function<dim>,
                        protected SolutionBase<dim>
  {
  public:
    RightHandSide () : Function<dim>() {}

    virtual double value (const Point<dim>   &p,
			  const unsigned int  component = 0) const;

  private:
    const ConvectionVelocity<dim> convection_velocity;
  };


  template <int dim>
  double RightHandSide<dim>::value (const Point<dim>   &p,
                                    const unsigned int) const
  {
    Tensor<1,dim> convection = convection_velocity.value(p);
    double return_value = 0;
    for (unsigned int i=0; i<this->n_source_centers; ++i)
      {
        const Point<dim> x_minus_xi = p - this->source_centers[i];

        return_value +=
          ((2*dim - 2*convection*x_minus_xi - 4*x_minus_xi.square()/
            (this->width * this->width)) /
           (this->width * this->width) *
           std::exp(-x_minus_xi.square() /
                    (this->width * this->width)));
      }

    return return_value / Utilities::fixed_power<dim>(std::sqrt(2 * numbers::PI)
                                                      * this->width);
  }



  template <int dim>
  class Step51
  {
  public:
    enum RefinementMode
    {
      global_refinement, adaptive_refinement
    };

    Step51 (const unsigned int degree,
            const RefinementMode refinement_mode);
    void run ();

  private:
    void setup_system ();
    void assemble_system (const bool reconstruct_trace = false);
    void solve ();
    void postprocess ();
    void refine_mesh ();
    void output_results (const unsigned int cycle);

    Triangulation<dim>   triangulation;

    const MappingQ<dim>  mapping;

    FESystem<dim>        fe_local;
    DoFHandler<dim>      dof_handler_local;

    FE_FaceQ<dim>        fe;
    DoFHandler<dim>      dof_handler;

    FE_DGQ<dim>          fe_u_post;
    DoFHandler<dim>      dof_handler_u_post;

    ConstraintMatrix     constraints;
    ChunkSparsityPattern sparsity_pattern;
    ChunkSparseMatrix<double> system_matrix;

    Vector<double>       solution;
    Vector<double>       system_rhs;

    Vector<double>       solution_local;
    Vector<double>       solution_u_post;

    const RefinementMode refinement_mode;

    ConvergenceTable     convergence_table;
  };



  template <int dim>
  Step51<dim>::Step51 (const unsigned int degree,
                       const RefinementMode refinement_mode) :
    mapping  (3),
    fe_local (FE_DGQ<dim>(degree), dim,
              FE_DGQ<dim>(degree), 1),
    dof_handler_local (triangulation),
    fe (degree),
    dof_handler (triangulation),
    fe_u_post (degree+1),
    dof_handler_u_post (triangulation),
    refinement_mode (refinement_mode)
  {}



  template <int dim>
  void
  Step51<dim>::setup_system ()
  {
    dof_handler_local.distribute_dofs(fe_local);
    dof_handler.distribute_dofs(fe);
    dof_handler_u_post.distribute_dofs(fe_u_post);

    std::cout << "   Number of degrees of freedom: "
              << dof_handler.n_dofs()
              << std::endl;

    solution.reinit (dof_handler.n_dofs());
    system_rhs.reinit (dof_handler.n_dofs());

    solution_local.reinit (dof_handler_local.n_dofs());
    solution_u_post.reinit (dof_handler_u_post.n_dofs());

    constraints.clear ();
    DoFTools::make_hanging_node_constraints (dof_handler, constraints);
    std::map<unsigned int,double> boundary_values;
    typename FunctionMap<dim>::type boundary_functions;
    Solution<dim> solution;
    boundary_functions[0] = &solution;
    VectorTools::project_boundary_values (mapping, dof_handler,
                                          boundary_functions,
                                          QGauss<dim-1>(fe.degree+1),
                                          boundary_values);
    for (std::map<unsigned int,double>::iterator it = boundary_values.begin();
         it != boundary_values.end(); ++it)
      if (constraints.is_constrained(it->first) == false)
        {
          constraints.add_line(it->first);
          constraints.set_inhomogeneity(it->first, it->second);
        }
    constraints.close ();

    {
      CompressedSimpleSparsityPattern csp (dof_handler.n_dofs());
      DoFTools::make_sparsity_pattern (dof_handler, csp,
                                       constraints, false);
      sparsity_pattern.copy_from(csp, fe.dofs_per_face);
    }
    system_matrix.reinit (sparsity_pattern);
  }



  template <int dim>
  void
  Step51<dim>::assemble_system (const bool trace_reconstruct)
  {
    QGauss<dim>   quadrature_formula(fe.degree+1);
    QGauss<dim-1> face_quadrature_formula(fe.degree+1);

    FEValues<dim> fe_values_local (mapping, fe_local, quadrature_formula,
                                   update_values | update_gradients |
                                   update_JxW_values | update_quadrature_points);
    FEFaceValues<dim> fe_face_values (mapping, fe, face_quadrature_formula,
                                      update_values | update_normal_vectors |
                                      update_quadrature_points |
                                      update_JxW_values);
    FEFaceValues<dim> fe_face_values_local (mapping, fe_local,
                                            face_quadrature_formula,
                                            update_values);

    const unsigned int n_q_points    = quadrature_formula.size();
    const unsigned int n_face_q_points = face_quadrature_formula.size();

    const unsigned int dofs_per_cell = fe.dofs_per_cell;
    const unsigned int loc_dofs_per_cell = fe_local.dofs_per_cell;

    FullMatrix<double> ll_matrix (loc_dofs_per_cell, loc_dofs_per_cell);
    FullMatrix<double> lf_matrix (loc_dofs_per_cell, dofs_per_cell);
    FullMatrix<double> fl_matrix (dofs_per_cell, loc_dofs_per_cell);
    FullMatrix<double> tmp_matrix (dofs_per_cell, loc_dofs_per_cell);
    FullMatrix<double> ff_matrix (dofs_per_cell, dofs_per_cell);
    Vector<double>     l_rhs (loc_dofs_per_cell);
    Vector<double>     f_rhs (dofs_per_cell);
    Vector<double>     tmp_rhs (loc_dofs_per_cell);

    std::vector<types::global_dof_index> dof_indices (dofs_per_cell);
    std::vector<types::global_dof_index> loc_dof_indices (loc_dofs_per_cell);

    ConvectionVelocity<dim> convection;
    std::vector<Tensor<1,dim> > convection_values (n_q_points);
    std::vector<Tensor<1,dim> > convection_values_face (n_face_q_points);

    std::vector<double> trace_values(n_face_q_points);

    // Choose stabilization parameter to be 5 * diffusion = 5
    const double tau_stab_diffusion = 5.;
    std::vector<double> tau_stab (n_q_points);

    RightHandSide<dim> right_hand_side;
    std::vector<double> rhs_values (n_q_points);

    const Solution<dim> exact_solution;
    std::vector<double> neumann_values (n_face_q_points);

    const FEValuesExtractors::Vector gradients (0);
    const FEValuesExtractors::Scalar values (dim);

    typename DoFHandler<dim>::active_cell_iterator
    cell = dof_handler.begin_active(),
    loc_cell = dof_handler_local.begin_active(),
    endc = dof_handler.end();
    for (; cell!=endc; ++cell, ++loc_cell)
      {
        if (!trace_reconstruct)
          {
            lf_matrix = 0;
            fl_matrix = 0;
            ff_matrix = 0;
            f_rhs = 0;
          }
        fe_values_local.reinit (loc_cell);
        right_hand_side.value_list (fe_values_local.get_quadrature_points(),
                                    rhs_values);
        convection.value_list(fe_values_local.get_quadrature_points(),
                              convection_values);

        for (unsigned int i=0; i<loc_dofs_per_cell; ++i)
          for (unsigned int j=0; j<loc_dofs_per_cell; ++j)
            {
              double sum = 0;
              for (unsigned int q=0; q<n_q_points; ++q)
                sum += (fe_values_local[gradients].value(i,q) *
                        fe_values_local[gradients].value(j,q)
                        -
                        fe_values_local[gradients].divergence(i,q) *
                        fe_values_local[values].value(j,q)
                        +
                        fe_values_local[values].value(i,q) *
                        fe_values_local[gradients].divergence(j,q)
                        -
                        fe_values_local[values].value(j,q) *
                        (fe_values_local[values].gradient(i,q) *
                         convection_values[q])
                        ) * fe_values_local.JxW(q);
              ll_matrix(i,j) = sum;
            }
        for (unsigned int i=0; i<loc_dofs_per_cell; ++i)
          {
            double sum = 0.;
            for (unsigned int q=0; q<n_q_points; ++q)
              sum += rhs_values[q] * fe_values_local.JxW(q) *
                fe_values_local[values].value(i,q);
            l_rhs(i) = sum;
          }

        for (unsigned int face=0; face<GeometryInfo<dim>::faces_per_cell; ++face)
          {
            fe_face_values_local.reinit(loc_cell, face);
            fe_face_values.reinit(cell, face);
            const std::vector<double> &JxW = fe_face_values.get_JxW_values();
            const std::vector<Point<dim> > &normals =
              fe_face_values.get_normal_vectors();
            convection.value_list(fe_face_values.get_quadrature_points(),
                                  convection_values_face);
            for (unsigned int q=0; q<n_face_q_points; ++q)
              tau_stab[q] = (tau_stab_diffusion +
                             std::abs(convection_values_face[q] * normals[q]));
            if (!trace_reconstruct)
              {
                for (unsigned int i=0; i<dofs_per_cell; ++i)
                  for (unsigned int j=0; j<loc_dofs_per_cell; ++j)
                    {
                      double sum_lf = 0., sum_fl = 0.;
                      for (unsigned int q=0; q<n_face_q_points; ++q)
                        {
                          sum_lf += (fe_face_values.shape_value(i,q) *
                                     (fe_face_values_local[gradients].value(j,q) *
                                      normals[q]
                                      +
                                      (convection_values_face[q] *
                                       normals[q]
                                       -
                                       tau_stab[q]) *
                                      fe_face_values_local[values].value(j,q))
                                     ) * JxW[q];
                          sum_fl += (fe_face_values.shape_value(i,q) *
                                     (fe_face_values_local[gradients].value(j,q) *
                                      normals[q]
                                      +
                                      tau_stab[q] *
                                      fe_face_values_local[values].value(j,q))
                                     ) * JxW[q];
                        }
                      lf_matrix(j,i) += sum_lf;
                      fl_matrix(i,j) -= sum_fl;
                    }
                for (unsigned int i=0; i<dofs_per_cell; ++i)
                  for (unsigned int j=0; j<dofs_per_cell; ++j)
                    {
                      double sum = 0;
                      for (unsigned int q=0; q<n_face_q_points; ++q)
                        sum += ((convection_values_face[q] * normals[q]
                                 -
                                 tau_stab[q]
                                 ) *
                                fe_face_values.shape_value(i,q) *
                                fe_face_values.shape_value(j,q)
                                ) * JxW[q];
                      ff_matrix(i,j) += sum;
                    }
                if (cell->face(face)->at_boundary()
                    &&
                    (cell->face(face)->boundary_indicator() == 1))
                  {
                    exact_solution.value_list(fe_face_values.get_quadrature_points(),
                                              neumann_values);
                    for (unsigned int i=0; i<dofs_per_cell; ++i)
                      {
                        double sum = 0;
                        for (unsigned int q=0; q<n_face_q_points; ++q)
                          sum -= (fe_face_values.shape_value(i,q) *
                                  neumann_values[q]) * JxW[q];
                        f_rhs(i) += sum;
                      }
                  }
              }
            for (unsigned int i=0; i<loc_dofs_per_cell; ++i)
              for (unsigned int j=0; j<loc_dofs_per_cell; ++j)
                {
                  double sum = 0;
                  for (unsigned int q=0; q<n_face_q_points; ++q)
                    sum += (tau_stab[q] *
                            fe_face_values_local[values].value(i,q) *
                            fe_face_values_local[values].value(j,q)) * JxW[q];
                  ll_matrix(i,j) += sum;
                }

            // compute the local right hand side contributions from trace
            if (trace_reconstruct)
              {
                fe_face_values.get_function_values (solution, trace_values);
                for (unsigned int i=0; i<loc_dofs_per_cell; ++i)
                  {
                    double sum = 0;
                    for (unsigned int q=0; q<n_face_q_points; ++q)
                      sum += ((fe_face_values_local[gradients].value(i,q) *
                               normals[q]) *
                              trace_values[q]
                              +
                              fe_face_values_local[values].value(i,q) *
                              (convection_values_face[q] * normals[q]
                               -
                               tau_stab[q]) * trace_values[q]) * JxW[q];
                    l_rhs(i) -= sum;
                  }
              }
          }

        ll_matrix.gauss_jordan();
        if (!trace_reconstruct)
          {
            fl_matrix.mmult(tmp_matrix, ll_matrix);
            tmp_matrix.vmult_add(f_rhs, l_rhs);
            tmp_matrix.mmult(ff_matrix, lf_matrix, true);
            cell->get_dof_indices(dof_indices);
            constraints.distribute_local_to_global (ff_matrix, f_rhs,
                                                    dof_indices,
                                                    system_matrix, system_rhs);
          }
        else
          {
            ll_matrix.vmult(tmp_rhs, l_rhs);
            loc_cell->set_dof_values(tmp_rhs, solution_local);
          }
      }
  }



  template <int dim>
  void Step51<dim>::solve ()
  {
    SolverControl solver_control (system_matrix.m()*10,
                                  1e-10*system_rhs.l2_norm());
    SolverGMRES<> solver (solver_control, 50);
    solver.solve (system_matrix, solution, system_rhs,
                  PreconditionIdentity());

    std::cout << "   Number of GMRES iterations: " << solver_control.last_step()
              << std::endl;

    system_matrix.clear();
    sparsity_pattern.reinit(0,0,0,1);
    constraints.distribute(solution);

    // update local values
    assemble_system(true);
  }



  template <int dim>
  void
  Step51<dim>::postprocess()
  {
    const unsigned int n_active_cells=triangulation.n_active_cells();
    Vector<float> difference_per_cell (triangulation.n_active_cells());

    ComponentSelectFunction<dim> value_select (dim, dim+1);
    VectorTools::integrate_difference (mapping, dof_handler_local,
                                       solution_local,
                                       SolutionAndGradient<dim>(),
                                       difference_per_cell,
                                       QGauss<dim>(fe.degree+2),
                                       VectorTools::L2_norm,
                                       &value_select);
    const double L2_error = difference_per_cell.l2_norm();

    ComponentSelectFunction<dim> gradient_select (std::pair<unsigned int,unsigned int>(0, dim),
                                                  dim+1);
    VectorTools::integrate_difference (mapping, dof_handler_local,
                                       solution_local,
                                       SolutionAndGradient<dim>(),
                                       difference_per_cell,
                                       QGauss<dim>(fe.degree+2),
                                       VectorTools::L2_norm,
                                       &gradient_select);
    const double grad_error = difference_per_cell.l2_norm();

    convergence_table.add_value("cells", n_active_cells);
    convergence_table.add_value("dofs", dof_handler.n_dofs());
    convergence_table.add_value("val L2", L2_error);
    convergence_table.add_value("grad L2", grad_error);

    // construct post-processed solution with (hopefully) higher order of
    // accuracy
    QGauss<dim> quadrature(fe_u_post.degree+1);
    FEValues<dim> fe_values(mapping, fe_u_post, quadrature,
                            update_values | update_JxW_values |
                            update_gradients);

    const unsigned int n_q_points = quadrature.size();
    std::vector<double> u_values(n_q_points);
    std::vector<Tensor<1,dim> > u_gradients(n_q_points);
    FEValuesExtractors::Vector gradients(0);
    FEValuesExtractors::Scalar values(dim);
    FEValues<dim> fe_values_local(mapping, fe_local, quadrature, update_values);
    FullMatrix<double> cell_matrix(fe_u_post.dofs_per_cell,
                                   fe_u_post.dofs_per_cell);
    Vector<double> cell_rhs(fe_u_post.dofs_per_cell);
    Vector<double> cell_sol(fe_u_post.dofs_per_cell);

    typename DoFHandler<dim>::active_cell_iterator
      cell_loc = dof_handler_local.begin_active(),
      cell = dof_handler_u_post.begin_active(),
      endc = dof_handler_u_post.end();
    for ( ; cell != endc; ++cell, ++cell_loc)
      {
        fe_values.reinit(cell);
        fe_values_local.reinit(cell_loc);

        fe_values_local[values].get_function_values(solution_local, u_values);
        fe_values_local[gradients].get_function_values(solution_local, u_gradients);
        for (unsigned int i=1; i<fe_u_post.dofs_per_cell; ++i)
          {
            for (unsigned int j=0; j<fe_u_post.dofs_per_cell; ++j)
              {
                double sum = 0;
                for (unsigned int q=0; q<quadrature.size(); ++q)
                  sum += (fe_values.shape_grad(i,q) *
                          fe_values.shape_grad(j,q)
                          ) * fe_values.JxW(q);
                cell_matrix(i,j) = sum;
              }
            double sum = 0;
            for (unsigned int q=0; q<quadrature.size(); ++q)
              sum -= (fe_values.shape_grad(i,q) * u_gradients[q]
                      ) * fe_values.JxW(q);
            cell_rhs(i) = sum;
          }
        for (unsigned int j=0; j<fe_u_post.dofs_per_cell; ++j)
          {
            double sum = 0;
            for (unsigned int q=0; q<quadrature.size(); ++q)
              sum += fe_values.shape_value(j,q) * fe_values.JxW(q);
            cell_matrix(0,j) = sum;
          }
        {
          double sum = 0;
          for (unsigned int q=0; q<quadrature.size(); ++q)
            sum += u_values[q] * fe_values.JxW(q);
          cell_rhs(0) = sum;
        }

        cell_matrix.gauss_jordan();
        cell_matrix.vmult(cell_sol, cell_rhs);
        cell->distribute_local_to_global(cell_sol, solution_u_post);
      }

    VectorTools::integrate_difference (mapping, dof_handler_u_post,
                                       solution_u_post,
                                       Solution<dim>(),
                                       difference_per_cell,
                                       QGauss<dim>(fe.degree+3),
                                       VectorTools::L2_norm);
    double post_error = difference_per_cell.l2_norm();
    convergence_table.add_value("val L2-post", post_error);
  }



  template <int dim>
  void Step51<dim>::output_results (const unsigned int cycle)
  {
    std::string filename;
    switch (refinement_mode)
      {
      case global_refinement:
        filename = "solution-global";
        break;
      case adaptive_refinement:
        filename = "solution-adaptive";
        break;
      default:
        Assert (false, ExcNotImplemented());
      }

    filename += "-q" + Utilities::int_to_string(fe.degree,1);
    filename += "-" + Utilities::int_to_string(cycle,2);
    filename += ".vtk";
    std::ofstream output (filename.c_str());

    DataOut<dim> data_out;
    std::vector<std::string> names (dim, "gradient");
    names.push_back ("solution");
    std::vector<DataComponentInterpretation::DataComponentInterpretation>
      component_interpretation
      (dim+1, DataComponentInterpretation::component_is_part_of_vector);
    component_interpretation[dim]
      = DataComponentInterpretation::component_is_scalar;
    data_out.add_data_vector (dof_handler_local, solution_local,
                              names, component_interpretation);

    data_out.build_patches (fe.degree);
    data_out.write_vtk (output);
  }




  template <int dim>
  void Step51<dim>::run ()
  {
    const bool do_cube = true;
    if (!do_cube)
      {
        GridGenerator::hyper_ball (triangulation);
        static const HyperBallBoundary<dim> boundary;
        triangulation.set_boundary(0, boundary);
        triangulation.refine_global(6-2*dim);
      }

    for (unsigned int cycle=0; cycle<10; ++cycle)
      {
        std::cout << "Cycle " << cycle << ':' << std::endl;

        if (do_cube)
          {
            triangulation.clear();
            GridGenerator::subdivided_hyper_cube (triangulation, 2+(cycle%2), -1, 1);
            triangulation.refine_global(3-dim+cycle/2);
          }
        else triangulation.refine_global(1);

        setup_system ();
        assemble_system (false);
        solve ();
        postprocess();
        output_results (cycle);
      }



    convergence_table.set_precision("val L2", 3);
    convergence_table.set_scientific("val L2", true);
    convergence_table.set_precision("grad L2", 3);
    convergence_table.set_scientific("grad L2", true);
    convergence_table.set_precision("val L2-post", 3);
    convergence_table.set_scientific("val L2-post", true);

    convergence_table
      .evaluate_convergence_rates("val L2", "cells", ConvergenceTable::reduction_rate_log2, dim);
    convergence_table
      .evaluate_convergence_rates("grad L2", "cells", ConvergenceTable::reduction_rate_log2, dim);
    convergence_table
      .evaluate_convergence_rates("val L2-post", "cells", ConvergenceTable::reduction_rate_log2, dim);
    convergence_table.write_text(std::cout);
  }
}


int main (int argc, char** argv)
{
  const unsigned int dim = 2;

  try
    {
      using namespace dealii;
      using namespace Step51;

      deallog.depth_console (0);

      // Now for the three calls to the main class in complete analogy to
      // step-7.
      {
        std::cout << "Solving with Q1 elements, adaptive refinement" << std::endl
                  << "=============================================" << std::endl
                  << std::endl;

        Step51<dim> hdg_problem (1, Step51<dim>::adaptive_refinement);
        hdg_problem.run ();

        std::cout << std::endl;
      }

      {
        std::cout << "Solving with Q1 elements, global refinement" << std::endl
                  << "===========================================" << std::endl
                  << std::endl;

        Step51<dim> hdg_problem (1, Step51<dim>::global_refinement);
        hdg_problem.run ();

        std::cout << std::endl;
      }

      {
        std::cout << "Solving with Q3 elements, global refinement" << std::endl
                  << "===========================================" << std::endl
                  << std::endl;

        Step51<dim> hdg_problem (3, Step51<dim>::global_refinement);
        hdg_problem.run ();

        std::cout << std::endl;
      }

    }
  catch (std::exception &exc)
    {
      std::cerr << std::endl << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Exception on processing: " << std::endl
                << exc.what() << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }
  catch (...)
    {
      std::cerr << std::endl << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Unknown exception!" << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }

  return 0;
}