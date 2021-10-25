/* ---------------------------------------------------------------------
 *
 * Copyright (C) 2021 by the deal.II authors
 *
 * This file is part of the deal.II library.
 *
 * The deal.II library is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE.md at
 * the top level directory of deal.II.
 *
 * ---------------------------------------------------------------------
 *
 * Authors: Andrea Bonito and Diane Guignard, 2021.
 */

#include <deal.II/grid/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_tools.h>

#include <deal.II/fe/fe_dgq.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/fe/fe_system.h>

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/function.h>

#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/data_out.h>

#include <deal.II/lac/vector.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/sparse_matrix.h>
#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/sparse_direct.h>
#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/precondition.h>

#include <fstream>
#include <iostream>


namespace Step82
{
  using namespace dealii;

  template <int dim>
  class BiLaplacianLDGLift
  {
  public:
    BiLaplacianLDGLift(const unsigned int n_refinements,
                       const unsigned int fe_degree,
                       const double       penalty_jump_grad,
                       const double       penalty_jump_val);

    void run();

  private:
    void make_grid();
    void setup_system();
    void assemble_system();
    void assemble_matrix();
    void assemble_rhs();

    void solve();

    void compute_errors();
    void output_results() const;

    void assemble_local_matrix(const FEValues<dim> &fe_values_lift,
                               const unsigned int   n_q_points,
                               FullMatrix<double> & local_matrix);

    void compute_discrete_hessians(
      const typename DoFHandler<dim>::active_cell_iterator &cell,
      std::vector<std::vector<Tensor<2, dim>>> &            discrete_hessians,
      std::vector<std::vector<std::vector<Tensor<2, dim>>>>
        &discrete_hessians_neigh);

    Triangulation<dim> triangulation;

    const unsigned int n_refinements;

    FE_DGQ<dim>     fe;
    DoFHandler<dim> dof_handler;

    FESystem<dim> fe_lift;

    SparsityPattern      sparsity_pattern;
    SparseMatrix<double> matrix;
    Vector<double>       rhs;
    Vector<double>       solution;

    const double penalty_jump_grad;
    const double penalty_jump_val;
  };



  template <int dim>
  class RightHandSide : public Function<dim>
  {
  public:
    RightHandSide()
      : Function<dim>()
    {}

    virtual double value(const Point<dim> & p,
                         const unsigned int component = 0) const override;
  };



  template <int dim>
  double RightHandSide<dim>::value(const Point<dim> &p,
                                   const unsigned int /*component*/) const
  {
    double return_value = 0.0;

    if (dim == 2)
      {
        return_value = 24.0 * std::pow(p(1) * (1.0 - p(1)), 2) +
                       +24.0 * std::pow(p(0) * (1.0 - p(0)), 2) +
                       2.0 * (2.0 - 12.0 * p(0) + 12.0 * p(0) * p(0)) *
                         (2.0 - 12.0 * p(1) + 12.0 * p(1) * p(1));
      }
    else if (dim == 3)
      {
        return_value =
          24.0 * std::pow(p(1) * (1.0 - p(1)) * p(2) * (1.0 - p(2)), 2) +
          24.0 * std::pow(p(0) * (1.0 - p(0)) * p(2) * (1.0 - p(2)), 2) +
          24.0 * std::pow(p(0) * (1.0 - p(0)) * p(1) * (1.0 - p(1)), 2) +
          2.0 * (2.0 - 12.0 * p(0) + 12.0 * p(0) * p(0)) *
            (2.0 - 12.0 * p(1) + 12.0 * p(1) * p(1)) *
            std::pow(p(2) * (1.0 - p(2)), 2) +
          2.0 * (2.0 - 12.0 * p(0) + 12.0 * p(0) * p(0)) *
            (2.0 - 12.0 * p(2) + 12.0 * p(2) * p(2)) *
            std::pow(p(1) * (1.0 - p(1)), 2) +
          2.0 * (2.0 - 12.0 * p(1) + 12.0 * p(1) * p(1)) *
            (2.0 - 12.0 * p(2) + 12.0 * p(2) * p(2)) *
            std::pow(p(0) * (1.0 - p(0)), 2);
      }
    else
      Assert(false, ExcNotImplemented());

    return return_value;
  }



  template <int dim>
  class ExactSolution : public Function<dim>
  {
  public:
    ExactSolution()
      : Function<dim>()
    {}

    virtual double value(const Point<dim> & p,
                         const unsigned int component = 0) const override;

    virtual Tensor<1, dim>
    gradient(const Point<dim> & p,
             const unsigned int component = 0) const override;

    virtual SymmetricTensor<2, dim>
    hessian(const Point<dim> & p,
            const unsigned int component = 0) const override;
  };



  template <int dim>
  double ExactSolution<dim>::value(const Point<dim> &p,
                                   const unsigned int /*component*/) const
  {
    double return_value = 0.0;

    if (dim == 2)
      {
        return_value = std::pow(p(0) * (1.0 - p(0)) * p(1) * (1.0 - p(1)), 2);
      }
    else if (dim == 3)
      {
        return_value = std::pow(p(0) * (1.0 - p(0)) * p(1) * (1.0 - p(1)) *
                                  p(2) * (1.0 - p(2)),
                                2);
      }
    else
      Assert(false, ExcNotImplemented());

    return return_value;
  }



  template <int dim>
  Tensor<1, dim>
  ExactSolution<dim>::gradient(const Point<dim> &p,
                               const unsigned int /*component*/) const
  {
    Tensor<1, dim> return_gradient;

    if (dim == 2)
      {
        return_gradient[0] =
          (2.0 * p(0) - 6.0 * std::pow(p(0), 2) + 4.0 * std::pow(p(0), 3)) *
          std::pow(p(1) * (1.0 - p(1)), 2);
        return_gradient[1] =
          (2.0 * p(1) - 6.0 * std::pow(p(1), 2) + 4.0 * std::pow(p(1), 3)) *
          std::pow(p(0) * (1.0 - p(0)), 2);
      }
    else if (dim == 3)
      {
        return_gradient[0] =
          (2.0 * p(0) - 6.0 * std::pow(p(0), 2) + 4.0 * std::pow(p(0), 3)) *
          std::pow(p(1) * (1.0 - p(1)) * p(2) * (1.0 - p(2)), 2);
        return_gradient[1] =
          (2.0 * p(1) - 6.0 * std::pow(p(1), 2) + 4.0 * std::pow(p(1), 3)) *
          std::pow(p(0) * (1.0 - p(0)) * p(2) * (1.0 - p(2)), 2);
        return_gradient[2] =
          (2.0 * p(2) - 6.0 * std::pow(p(2), 2) + 4.0 * std::pow(p(2), 3)) *
          std::pow(p(0) * (1.0 - p(0)) * p(1) * (1.0 - p(1)), 2);
      }
    else
      Assert(false, ExcNotImplemented());

    return return_gradient;
  }



  template <int dim>
  SymmetricTensor<2, dim>
  ExactSolution<dim>::hessian(const Point<dim> &p,
                              const unsigned int /*component*/) const
  {
    SymmetricTensor<2, dim> return_hessian;

    if (dim == 2)
      {
        return_hessian[0][0] = (2.0 - 12.0 * p(0) + 12.0 * p(0) * p(0)) *
                               std::pow(p(1) * (1.0 - p(1)), 2);
        return_hessian[0][1] =
          (2.0 * p(0) - 6.0 * std::pow(p(0), 2) + 4.0 * std::pow(p(0), 3)) *
          (2.0 * p(1) - 6.0 * std::pow(p(1), 2) + 4.0 * std::pow(p(1), 3));
        return_hessian[1][1] = (2.0 - 12.0 * p(1) + 12.0 * p(1) * p(1)) *
                               std::pow(p(0) * (1.0 - p(0)), 2);
      }
    else if (dim == 3)
      {
        return_hessian[0][0] =
          (2.0 - 12.0 * p(0) + 12.0 * p(0) * p(0)) *
          std::pow(p(1) * (1.0 - p(1)) * p(2) * (1.0 - p(2)), 2);
        return_hessian[0][1] =
          (2.0 * p(0) - 6.0 * std::pow(p(0), 2) + 4.0 * std::pow(p(0), 3)) *
          (2.0 * p(1) - 6.0 * std::pow(p(1), 2) + 4.0 * std::pow(p(1), 3)) *
          std::pow(p(2) * (1.0 - p(2)), 2);
        return_hessian[0][2] =
          (2.0 * p(0) - 6.0 * std::pow(p(0), 2) + 4.0 * std::pow(p(0), 3)) *
          (2.0 * p(2) - 6.0 * std::pow(p(2), 2) + 4.0 * std::pow(p(2), 3)) *
          std::pow(p(1) * (1.0 - p(1)), 2);
        return_hessian[1][1] =
          (2.0 - 12.0 * p(1) + 12.0 * p(1) * p(1)) *
          std::pow(p(0) * (1.0 - p(0)) * p(2) * (1.0 - p(2)), 2);
        return_hessian[1][2] =
          (2.0 * p(1) - 6.0 * std::pow(p(1), 2) + 4.0 * std::pow(p(1), 3)) *
          (2.0 * p(2) - 6.0 * std::pow(p(2), 2) + 4.0 * std::pow(p(2), 3)) *
          std::pow(p(0) * (1.0 - p(0)), 2);
        return_hessian[2][2] =
          (2.0 - 12.0 * p(2) + 12.0 * p(2) * p(2)) *
          std::pow(p(0) * (1.0 - p(0)) * p(1) * (1.0 - p(1)), 2);
      }
    else
      Assert(false, ExcNotImplemented());

    return return_hessian;
  }



  template <int dim>
  BiLaplacianLDGLift<dim>::BiLaplacianLDGLift(const unsigned int n_refinements,
                                              const unsigned int fe_degree,
                                              const double penalty_jump_grad,
                                              const double penalty_jump_val)
    : n_refinements(n_refinements)
    , fe(fe_degree)
    , dof_handler(triangulation)
    , fe_lift(FE_DGQ<dim>(fe_degree), dim * dim)
    , penalty_jump_grad(penalty_jump_grad)
    , penalty_jump_val(penalty_jump_val)
  {}



  template <int dim>
  void BiLaplacianLDGLift<dim>::make_grid()
  {
    std::cout << "Building the mesh............." << std::endl;

    GridGenerator::hyper_cube(triangulation, 0.0, 1.0);

    triangulation.refine_global(n_refinements);

    std::cout << "Number of active cells: " << triangulation.n_active_cells()
              << std::endl;
  }



  template <int dim>
  void BiLaplacianLDGLift<dim>::setup_system()
  {
    dof_handler.distribute_dofs(fe);

    std::cout << "Number of degrees of freedom: " << dof_handler.n_dofs()
              << std::endl;

    DynamicSparsityPattern dsp(dof_handler.n_dofs(), dof_handler.n_dofs());

    const auto dofs_per_cell = fe.dofs_per_cell;

    for (const auto &cell : dof_handler.active_cell_iterators())
      {
        std::vector<types::global_dof_index> dofs(dofs_per_cell);
        cell->get_dof_indices(dofs);

        for (unsigned int f = 0; f < cell->n_faces(); ++f)
          if (!cell->face(f)->at_boundary())
            {
              const auto neighbor_cell = cell->neighbor(f);

              std::vector<types::global_dof_index> tmp(dofs_per_cell);
              neighbor_cell->get_dof_indices(tmp);

              dofs.insert(std::end(dofs), std::begin(tmp), std::end(tmp));
            }

        for (const auto i : dofs)
          for (const auto j : dofs)
            {
              dsp.add(i, j);
              dsp.add(j, i);
            }
      }

    sparsity_pattern.copy_from(dsp);


    matrix.reinit(sparsity_pattern);
    rhs.reinit(dof_handler.n_dofs());

    solution.reinit(dof_handler.n_dofs());

    std::ofstream out("sparsity_pattern.svg");
    sparsity_pattern.print_svg(out);
  }



  template <int dim>
  void BiLaplacianLDGLift<dim>::assemble_system()
  {
    std::cout << "Assembling the system............." << std::endl;

    assemble_matrix();
    assemble_rhs();

    std::cout << "Done. " << std::endl;
  }



  template <int dim>
  void BiLaplacianLDGLift<dim>::assemble_matrix()
  {
    matrix = 0;

    QGauss<dim>     quad(fe.degree + 1);
    QGauss<dim - 1> quad_face(fe.degree + 1);

    const unsigned int n_q_points      = quad.size();
    const unsigned int n_q_points_face = quad_face.size();

    FEValues<dim> fe_values(fe, quad, update_hessians | update_JxW_values);

    FEFaceValues<dim> fe_face(
      fe, quad_face, update_values | update_gradients | update_normal_vectors);

    FEFaceValues<dim> fe_face_neighbor(
      fe, quad_face, update_values | update_gradients | update_normal_vectors);

    const unsigned int n_dofs = fe_values.dofs_per_cell;

    std::vector<types::global_dof_index> local_dof_indices(n_dofs);
    std::vector<types::global_dof_index> local_dof_indices_neighbor(n_dofs);
    std::vector<types::global_dof_index> local_dof_indices_neighbor_2(n_dofs);

    FullMatrix<double> stiffness_matrix_cc(n_dofs,
                                           n_dofs); // interactions cell / cell
    FullMatrix<double> stiffness_matrix_cn(
      n_dofs, n_dofs); // interactions cell / neighbor
    FullMatrix<double> stiffness_matrix_nc(
      n_dofs, n_dofs); // interactions neighbor / cell
    FullMatrix<double> stiffness_matrix_nn(
      n_dofs, n_dofs); // interactions neighbor / neighbor
    FullMatrix<double> stiffness_matrix_n1n2(
      n_dofs, n_dofs); // interactions neighbor1 / neighbor2
    FullMatrix<double> stiffness_matrix_n2n1(
      n_dofs, n_dofs); // interactions neighbor2 / neighbor1

    FullMatrix<double> ip_matrix_cc(n_dofs, n_dofs); // interactions cell / cell
    FullMatrix<double> ip_matrix_cn(n_dofs,
                                    n_dofs); // interactions cell / neighbor
    FullMatrix<double> ip_matrix_nc(n_dofs,
                                    n_dofs); // interactions neighbor / cell
    FullMatrix<double> ip_matrix_nn(n_dofs,
                                    n_dofs); // interactions neighbor / neighbor

    std::vector<std::vector<Tensor<2, dim>>> discrete_hessians(
      n_dofs, std::vector<Tensor<2, dim>>(n_q_points));
    std::vector<std::vector<std::vector<Tensor<2, dim>>>>
      discrete_hessians_neigh(GeometryInfo<dim>::faces_per_cell,
                              discrete_hessians);

    for (const auto &cell : dof_handler.active_cell_iterators())
      {
        fe_values.reinit(cell);
        cell->get_dof_indices(local_dof_indices);

        compute_discrete_hessians(cell,
                                  discrete_hessians,
                                  discrete_hessians_neigh);

        stiffness_matrix_cc = 0;
        for (unsigned int q = 0; q < n_q_points; ++q)
          {
            const double dx = fe_values.JxW(q);

            for (unsigned int i = 0; i < n_dofs; ++i)
              for (unsigned int j = 0; j < n_dofs; ++j)
                {
                  const Tensor<2, dim> &H_i = discrete_hessians[i][q];
                  const Tensor<2, dim> &H_j = discrete_hessians[j][q];

                  stiffness_matrix_cc(i, j) += scalar_product(H_j, H_i) * dx;
                }
          }

        for (unsigned int i = 0; i < n_dofs; ++i)
          for (unsigned int j = 0; j < n_dofs; ++j)
            {
              matrix(local_dof_indices[i], local_dof_indices[j]) +=
                stiffness_matrix_cc(i, j);
            }

        for (unsigned int face_no = 0; face_no < cell->n_faces(); ++face_no)
          {
            const typename DoFHandler<dim>::face_iterator face =
              cell->face(face_no);

            const bool at_boundary = face->at_boundary();
            if (!at_boundary)
              {
                const typename DoFHandler<dim>::active_cell_iterator
                  neighbor_cell = cell->neighbor(face_no);
                neighbor_cell->get_dof_indices(local_dof_indices_neighbor);

                stiffness_matrix_cn = 0;
                stiffness_matrix_nc = 0;
                stiffness_matrix_nn = 0;
                for (unsigned int q = 0; q < n_q_points; ++q)
                  {
                    const double dx = fe_values.JxW(q);

                    for (unsigned int i = 0; i < n_dofs; ++i)
                      {
                        for (unsigned int j = 0; j < n_dofs; ++j)
                          {
                            const Tensor<2, dim> &H_i = discrete_hessians[i][q];
                            const Tensor<2, dim> &H_j = discrete_hessians[j][q];

                            const Tensor<2, dim> &H_i_neigh =
                              discrete_hessians_neigh[face_no][i][q];
                            const Tensor<2, dim> &H_j_neigh =
                              discrete_hessians_neigh[face_no][j][q];

                            stiffness_matrix_cn(i, j) +=
                              scalar_product(H_j_neigh, H_i) * dx;
                            stiffness_matrix_nc(i, j) +=
                              scalar_product(H_j, H_i_neigh) * dx;
                            stiffness_matrix_nn(i, j) +=
                              scalar_product(H_j_neigh, H_i_neigh) * dx;
                          }
                      }
                  }

                for (unsigned int i = 0; i < n_dofs; ++i)
                  {
                    for (unsigned int j = 0; j < n_dofs; ++j)
                      {
                        matrix(local_dof_indices[i],
                               local_dof_indices_neighbor[j]) +=
                          stiffness_matrix_cn(i, j);
                        matrix(local_dof_indices_neighbor[i],
                               local_dof_indices[j]) +=
                          stiffness_matrix_nc(i, j);
                        matrix(local_dof_indices_neighbor[i],
                               local_dof_indices_neighbor[j]) +=
                          stiffness_matrix_nn(i, j);
                      }
                  }

              } // boundary check
          }     // for face

        for (unsigned int face_no = 0; face_no < cell->n_faces() - 1; ++face_no)
          {
            const typename DoFHandler<dim>::face_iterator face =
              cell->face(face_no);

            const bool at_boundary = face->at_boundary();
            if (!at_boundary)
              {

                for (unsigned int face_no_2 = face_no + 1;
                     face_no_2 < cell->n_faces();
                     ++face_no_2)
                  {
                    const typename DoFHandler<dim>::face_iterator face_2 =
                      cell->face(face_no_2);

                    const bool at_boundary_2 = face_2->at_boundary();
                    if (!at_boundary_2)
                      {
                        const typename DoFHandler<dim>::active_cell_iterator
                          neighbor_cell = cell->neighbor(face_no);
                        neighbor_cell->get_dof_indices(
                          local_dof_indices_neighbor);
                        const typename DoFHandler<dim>::active_cell_iterator
                          neighbor_cell_2 = cell->neighbor(face_no_2);
                        neighbor_cell_2->get_dof_indices(
                          local_dof_indices_neighbor_2);

                        stiffness_matrix_n1n2 = 0;
                        stiffness_matrix_n2n1 = 0;

                        for (unsigned int q = 0; q < n_q_points; ++q)
                          {
                            const double dx = fe_values.JxW(q);

                            for (unsigned int i = 0; i < n_dofs; ++i)
                              for (unsigned int j = 0; j < n_dofs; ++j)
                                {
                                  const Tensor<2, dim> &H_i_neigh =
                                    discrete_hessians_neigh[face_no][i][q];
                                  const Tensor<2, dim> &H_j_neigh =
                                    discrete_hessians_neigh[face_no][j][q];

                                  const Tensor<2, dim> &H_i_neigh2 =
                                    discrete_hessians_neigh[face_no_2][i][q];
                                  const Tensor<2, dim> &H_j_neigh2 =
                                    discrete_hessians_neigh[face_no_2][j][q];

                                  stiffness_matrix_n1n2(i, j) +=
                                    scalar_product(H_j_neigh2, H_i_neigh) * dx;
                                  stiffness_matrix_n2n1(i, j) +=
                                    scalar_product(H_j_neigh, H_i_neigh2) * dx;
                                }
                          }

                        for (unsigned int i = 0; i < n_dofs; ++i)
                          for (unsigned int j = 0; j < n_dofs; ++j)
                            {
                              matrix(local_dof_indices_neighbor[i],
                                     local_dof_indices_neighbor_2[j]) +=
                                stiffness_matrix_n1n2(i, j);
                              matrix(local_dof_indices_neighbor_2[i],
                                     local_dof_indices_neighbor[j]) +=
                                stiffness_matrix_n2n1(i, j);
                            }
                      } // boundary check face_2
                  }     // for face_2
              }         // boundary check face_1
          }             // for face_1


        for (unsigned int face_no = 0; face_no < cell->n_faces(); ++face_no)
          {
            const typename DoFHandler<dim>::face_iterator face =
              cell->face(face_no);

            const double mesh_inv = 1.0 / face->diameter(); // h_e^{-1}
            const double mesh3_inv =
              1.0 / std::pow(face->diameter(), 3); // ĥ_e^{-3}

            fe_face.reinit(cell, face_no);

            ip_matrix_cc = 0; // filled in any case (boundary or interior face)

            const bool at_boundary = face->at_boundary();
            if (at_boundary)
              {
                for (unsigned int q = 0; q < n_q_points_face; ++q)
                  {
                    const double dx = fe_face.JxW(q);

                    for (unsigned int i = 0; i < n_dofs; ++i)
                      for (unsigned int j = 0; j < n_dofs; ++j)
                        {
                          ip_matrix_cc(i, j) += penalty_jump_grad * mesh_inv *
                                                fe_face.shape_grad(j, q) *
                                                fe_face.shape_grad(i, q) * dx;
                          ip_matrix_cc(i, j) += penalty_jump_val * mesh3_inv *
                                                fe_face.shape_value(j, q) *
                                                fe_face.shape_value(i, q) * dx;
                        }
                  }
              }
            else
              { // interior face

                const typename DoFHandler<dim>::active_cell_iterator
                                   neighbor_cell = cell->neighbor(face_no);
                const unsigned int face_no_neighbor =
                  cell->neighbor_of_neighbor(face_no);

                if (neighbor_cell->id() < cell->id())
                  continue; // skip this face (already considered)
                else
                  {
                    fe_face_neighbor.reinit(neighbor_cell, face_no_neighbor);
                    neighbor_cell->get_dof_indices(local_dof_indices_neighbor);

                    ip_matrix_cn = 0;
                    ip_matrix_nc = 0;
                    ip_matrix_nn = 0;

                    for (unsigned int q = 0; q < n_q_points_face; ++q)
                      {
                        const double dx = fe_face.JxW(q);

                        for (unsigned int i = 0; i < n_dofs; ++i)
                          {
                            for (unsigned int j = 0; j < n_dofs; ++j)
                              {
                                ip_matrix_cc(i, j) +=
                                  penalty_jump_grad * mesh_inv *
                                  fe_face.shape_grad(j, q) *
                                  fe_face.shape_grad(i, q) * dx;
                                ip_matrix_cc(i, j) +=
                                  penalty_jump_val * mesh3_inv *
                                  fe_face.shape_value(j, q) *
                                  fe_face.shape_value(i, q) * dx;

                                ip_matrix_cn(i, j) -=
                                  penalty_jump_grad * mesh_inv *
                                  fe_face_neighbor.shape_grad(j, q) *
                                  fe_face.shape_grad(i, q) * dx;
                                ip_matrix_cn(i, j) -=
                                  penalty_jump_val * mesh3_inv *
                                  fe_face_neighbor.shape_value(j, q) *
                                  fe_face.shape_value(i, q) * dx;

                                ip_matrix_nc(i, j) -=
                                  penalty_jump_grad * mesh_inv *
                                  fe_face.shape_grad(j, q) *
                                  fe_face_neighbor.shape_grad(i, q) * dx;
                                ip_matrix_nc(i, j) -=
                                  penalty_jump_val * mesh3_inv *
                                  fe_face.shape_value(j, q) *
                                  fe_face_neighbor.shape_value(i, q) * dx;

                                ip_matrix_nn(i, j) +=
                                  penalty_jump_grad * mesh_inv *
                                  fe_face_neighbor.shape_grad(j, q) *
                                  fe_face_neighbor.shape_grad(i, q) * dx;
                                ip_matrix_nn(i, j) +=
                                  penalty_jump_val * mesh3_inv *
                                  fe_face_neighbor.shape_value(j, q) *
                                  fe_face_neighbor.shape_value(i, q) * dx;
                              }
                          }
                      }
                  } // face not visited yet

              } // boundary check

            for (unsigned int i = 0; i < n_dofs; ++i)
              {
                for (unsigned int j = 0; j < n_dofs; ++j)
                  {
                    matrix(local_dof_indices[i], local_dof_indices[j]) +=
                      ip_matrix_cc(i, j);
                  }
              }

            if (!at_boundary)
              {
                for (unsigned int i = 0; i < n_dofs; ++i)
                  {
                    for (unsigned int j = 0; j < n_dofs; ++j)
                      {
                        matrix(local_dof_indices[i],
                               local_dof_indices_neighbor[j]) +=
                          ip_matrix_cn(i, j);
                        matrix(local_dof_indices_neighbor[i],
                               local_dof_indices[j]) += ip_matrix_nc(i, j);
                        matrix(local_dof_indices_neighbor[i],
                               local_dof_indices_neighbor[j]) +=
                          ip_matrix_nn(i, j);
                      }
                  }
              }

          } // for face
      }     // for cell
  }



  template <int dim>
  void BiLaplacianLDGLift<dim>::assemble_rhs()
  {
    rhs = 0;

    const QGauss<dim> quad(fe.degree + 1);
    FEValues<dim>     fe_values(
      fe, quad, update_values | update_quadrature_points | update_JxW_values);

    const unsigned int n_dofs     = fe_values.dofs_per_cell;
    const unsigned int n_quad_pts = quad.size();

    const RightHandSide<dim> right_hand_side;

    Vector<double>                       local_rhs(n_dofs);
    std::vector<types::global_dof_index> local_dof_indices(n_dofs);

    for (const auto &cell : dof_handler.active_cell_iterators())
      {
        fe_values.reinit(cell);
        cell->get_dof_indices(local_dof_indices);

        local_rhs = 0;
        for (unsigned int q = 0; q < n_quad_pts; ++q)
          {
            const double dx = fe_values.JxW(q);

            for (unsigned int i = 0; i < n_dofs; ++i)
              {
                local_rhs(i) +=
                  right_hand_side.value(fe_values.quadrature_point(q)) *
                  fe_values.shape_value(i, q) * dx;
              }
          }

        for (unsigned int i = 0; i < n_dofs; ++i)
          rhs(local_dof_indices[i]) += local_rhs(i);
      }
  }



  template <int dim>
  void BiLaplacianLDGLift<dim>::solve()
  {
    SparseDirectUMFPACK A_direct;
    A_direct.initialize(matrix);
    A_direct.vmult(solution, rhs);
  }



  template <int dim>
  void BiLaplacianLDGLift<dim>::compute_errors()
  {
    double error_H2 = 0;
    double error_H1 = 0;
    double error_L2 = 0;

    QGauss<dim>     quad(fe.degree + 1);
    QGauss<dim - 1> quad_face(fe.degree + 1);

    FEValues<dim> fe_values(fe,
                            quad,
                            update_values | update_gradients | update_hessians |
                              update_quadrature_points | update_JxW_values);

    FEFaceValues<dim> fe_face(fe,
                              quad_face,
                              update_values | update_gradients |
                                update_quadrature_points | update_JxW_values);

    FEFaceValues<dim> fe_face_neighbor(fe,
                                       quad_face,
                                       update_values | update_gradients);

    const unsigned int n_q_points      = quad.size();
    const unsigned int n_q_points_face = quad_face.size();

    const ExactSolution<dim> u_exact;

    std::vector<double>         solution_values_cell(n_q_points);
    std::vector<Tensor<1, dim>> solution_gradients_cell(n_q_points);
    std::vector<Tensor<2, dim>> solution_hessians_cell(n_q_points);

    std::vector<double>         solution_values(n_q_points_face);
    std::vector<double>         solution_values_neigh(n_q_points_face);
    std::vector<Tensor<1, dim>> solution_gradients(n_q_points_face);
    std::vector<Tensor<1, dim>> solution_gradients_neigh(n_q_points_face);

    for (const auto &cell : dof_handler.active_cell_iterators())
      {
        fe_values.reinit(cell);

        fe_values.get_function_values(solution, solution_values_cell);
        fe_values.get_function_gradients(solution, solution_gradients_cell);
        fe_values.get_function_hessians(solution, solution_hessians_cell);

        for (unsigned int q = 0; q < n_q_points; ++q)
          {
            const double dx = fe_values.JxW(q);

            error_H2 += (u_exact.hessian(fe_values.quadrature_point(q)) -
                         solution_hessians_cell[q])
                          .norm_square() *
                        dx;
            error_H1 += (u_exact.gradient(fe_values.quadrature_point(q)) -
                         solution_gradients_cell[q])
                          .norm_square() *
                        dx;
            error_L2 += std::pow(u_exact.value(fe_values.quadrature_point(q)) -
                                   solution_values_cell[q],
                                 2) *
                        dx;
          } // for quadrature points

        for (unsigned int face_no = 0; face_no < cell->n_faces(); ++face_no)
          {
            const typename DoFHandler<dim>::face_iterator face =
              cell->face(face_no);

            const double mesh_inv = 1.0 / face->diameter(); // h^{-1}
            const double mesh3_inv =
              1.0 / std::pow(face->diameter(), 3); // h^{-3}

            fe_face.reinit(cell, face_no);

            fe_face.get_function_values(solution, solution_values);
            fe_face.get_function_gradients(solution, solution_gradients);

            const bool at_boundary = face->at_boundary();
            if (at_boundary)
              {
                for (unsigned int q = 0; q < n_q_points_face; ++q)
                  {
                    const double dx = fe_face.JxW(q);
                    const double u_exact_q =
                      u_exact.value(fe_face.quadrature_point(q));
                    const Tensor<1, dim> u_exact_grad_q =
                      u_exact.gradient(fe_face.quadrature_point(q));

                    error_H2 +=
                      mesh_inv *
                      (u_exact_grad_q - solution_gradients[q]).norm_square() *
                      dx;
                    error_H2 += mesh3_inv *
                                std::pow(u_exact_q - solution_values[q], 2) *
                                dx;
                    error_H1 += mesh_inv *
                                std::pow(u_exact_q - solution_values[q], 2) *
                                dx;
                  }
              }
            else
              { // interior face

                const typename DoFHandler<dim>::active_cell_iterator
                                   neighbor_cell = cell->neighbor(face_no);
                const unsigned int face_no_neighbor =
                  cell->neighbor_of_neighbor(face_no);

                if (neighbor_cell->id() < cell->id())
                  continue; // skip this face (already considered)
                else
                  {
                    fe_face_neighbor.reinit(neighbor_cell, face_no_neighbor);

                    fe_face.get_function_values(solution, solution_values);
                    fe_face_neighbor.get_function_values(solution,
                                                         solution_values_neigh);
                    fe_face.get_function_gradients(solution,
                                                   solution_gradients);
                    fe_face_neighbor.get_function_gradients(
                      solution, solution_gradients_neigh);

                    for (unsigned int q = 0; q < n_q_points_face; ++q)
                      {
                        const double dx = fe_face.JxW(q);

                        error_H2 +=
                          mesh_inv *
                          (solution_gradients_neigh[q] - solution_gradients[q])
                            .norm_square() *
                          dx;
                        error_H2 += mesh3_inv *
                                    std::pow(solution_values_neigh[q] -
                                               solution_values[q],
                                             2) *
                                    dx;
                        error_H1 += mesh_inv *
                                    std::pow(solution_values_neigh[q] -
                                               solution_values[q],
                                             2) *
                                    dx;
                      }
                  } // face not visited yet

              } // boundary check

          } // for face

      } // for cell

    error_H2 = std::sqrt(error_H2);
    error_H1 = std::sqrt(error_H1);
    error_L2 = std::sqrt(error_L2);

    std::cout << "DG H2 norm of the error: " << error_H2 << std::endl;
    std::cout << "DG H1 norm of the error: " << error_H1 << std::endl;
    std::cout << "   L2 norm of the error: " << error_L2 << std::endl;
  }



  template <int dim>
  void BiLaplacianLDGLift<dim>::output_results() const
  {
    DataOut<dim> data_out;
    data_out.attach_dof_handler(dof_handler);
    data_out.add_data_vector(solution, "solution");
    data_out.build_patches();

    std::ofstream output("solution.vtk");
    data_out.write_vtk(output);
  }



  template <int dim>
  void BiLaplacianLDGLift<dim>::assemble_local_matrix(
    const FEValues<dim> &fe_values_lift,
    const unsigned int   n_q_points,
    FullMatrix<double> & local_matrix)
  {
    const FEValuesExtractors::Tensor<2> tau_ext(0);

    const unsigned int n_dofs = fe_values_lift.dofs_per_cell;

    local_matrix = 0;
    for (unsigned int q = 0; q < n_q_points; ++q)
      {
        const double dx = fe_values_lift.JxW(q);

        for (unsigned int m = 0; m < n_dofs; ++m)
          for (unsigned int n = 0; n < n_dofs; ++n)
            {
              local_matrix(m, n) +=
                scalar_product(fe_values_lift[tau_ext].value(n, q),
                               fe_values_lift[tau_ext].value(m, q)) *
                dx;
            }
      }
  }



  template <int dim>
  void BiLaplacianLDGLift<dim>::compute_discrete_hessians(
    const typename DoFHandler<dim>::active_cell_iterator &cell,
    std::vector<std::vector<Tensor<2, dim>>> &            discrete_hessians,
    std::vector<std::vector<std::vector<Tensor<2, dim>>>>
      &discrete_hessians_neigh)
  {
    const typename Triangulation<dim>::cell_iterator cell_lift =
      static_cast<typename Triangulation<dim>::cell_iterator>(cell);

    QGauss<dim>     quad(fe.degree + 1);
    QGauss<dim - 1> quad_face(fe.degree + 1);

    const unsigned int n_q_points      = quad.size();
    const unsigned int n_q_points_face = quad_face.size();

    FEValues<dim> fe_values(fe, quad, update_hessians | update_JxW_values);

    FEFaceValues<dim> fe_face(
      fe, quad_face, update_values | update_gradients | update_normal_vectors);

    FEFaceValues<dim> fe_face_neighbor(
      fe, quad_face, update_values | update_gradients | update_normal_vectors);

    const unsigned int n_dofs = fe_values.dofs_per_cell;

    FEValues<dim> fe_values_lift(fe_lift,
                                 quad,
                                 update_values | update_JxW_values);

    FEFaceValues<dim> fe_face_lift(
      fe_lift, quad_face, update_values | update_gradients | update_JxW_values);

    const FEValuesExtractors::Tensor<2> tau_ext(0);

    const unsigned int n_dofs_lift = fe_values_lift.dofs_per_cell;
    FullMatrix<double> local_matrix_lift(n_dofs_lift, n_dofs_lift);

    Vector<double> local_rhs_re(n_dofs_lift), local_rhs_be(n_dofs_lift),
      coeffs_re(n_dofs_lift), coeffs_be(n_dofs_lift), coeffs_tmp(n_dofs_lift);

    SolverControl            solver_control(1000, 1e-12);
    SolverCG<Vector<double>> solver(solver_control);

    double factor_avg; // 0.5 for interior faces, 1.0 for boundary faces

    fe_values.reinit(cell);
    fe_values_lift.reinit(cell_lift);

    assemble_local_matrix(fe_values_lift, n_q_points, local_matrix_lift);

    for (unsigned int i = 0; i < n_dofs; ++i)
      for (unsigned int q = 0; q < n_q_points; ++q)
        {
          discrete_hessians[i][q] = 0;

          for (unsigned int face_no = 0;
               face_no < discrete_hessians_neigh.size();
               ++face_no)
            {
              discrete_hessians_neigh[face_no][i][q] = 0;
            }
        }

    for (unsigned int i = 0; i < n_dofs; ++i)
      {
        coeffs_re = 0;
        coeffs_be = 0;

        for (unsigned int face_no = 0; face_no < cell->n_faces(); ++face_no)
          {
            const typename DoFHandler<dim>::face_iterator face =
              cell->face(face_no);

            const bool at_boundary = face->at_boundary();

            factor_avg = 0.5;
            if (at_boundary)
              {
                factor_avg = 1.0;
              }

            fe_face.reinit(cell, face_no);
            fe_face_lift.reinit(cell_lift, face_no);

            local_rhs_re = 0;
            for (unsigned int q = 0; q < n_q_points_face; ++q)
              {
                const double         dx     = fe_face_lift.JxW(q);
                const Tensor<1, dim> normal = fe_face.normal_vector(
                  q); // same as fe_face_lift.normal_vector(q)

                for (unsigned int m = 0; m < n_dofs_lift; ++m)
                  {
                    local_rhs_re(m) +=
                      factor_avg *
                      (fe_face_lift[tau_ext].value(m, q) * normal) *
                      fe_face.shape_grad(i, q) * dx;
                  }
              }

            local_rhs_be = 0;
            for (unsigned int q = 0; q < n_q_points_face; ++q)
              {
                const double         dx     = fe_face_lift.JxW(q);
                const Tensor<1, dim> normal = fe_face.normal_vector(
                  q); // same as fe_face_lift.normal_vector(q)

                for (unsigned int m = 0; m < n_dofs_lift; ++m)
                  {
                    local_rhs_be(m) += factor_avg *
                                       fe_face_lift[tau_ext].divergence(m, q) *
                                       normal * fe_face.shape_value(i, q) * dx;
                  }
              }

            coeffs_tmp = 0;
            solver.solve(local_matrix_lift,
                         coeffs_tmp,
                         local_rhs_re,
                         PreconditionIdentity());
            coeffs_re += coeffs_tmp;

            coeffs_tmp = 0;
            solver.solve(local_matrix_lift,
                         coeffs_tmp,
                         local_rhs_be,
                         PreconditionIdentity());
            coeffs_be += coeffs_tmp;

          } // for face

        for (unsigned int q = 0; q < n_q_points; ++q)
          {
            discrete_hessians[i][q] += fe_values.shape_hessian(i, q);

            for (unsigned int m = 0; m < n_dofs_lift; ++m)
              {
                discrete_hessians[i][q] -=
                  coeffs_re[m] * fe_values_lift[tau_ext].value(m, q);
              }

            for (unsigned int m = 0; m < n_dofs_lift; ++m)
              {
                discrete_hessians[i][q] +=
                  coeffs_be[m] * fe_values_lift[tau_ext].value(m, q);
              }
          }
      } // for dof i



    for (unsigned int face_no = 0; face_no < cell->n_faces(); ++face_no)
      {
        const typename DoFHandler<dim>::face_iterator face =
          cell->face(face_no);

        const bool at_boundary = face->at_boundary();

        if (!at_boundary)
          {
            const typename DoFHandler<2, dim>::active_cell_iterator
                               neighbor_cell = cell->neighbor(face_no);
            const unsigned int face_no_neighbor =
              cell->neighbor_of_neighbor(face_no);
            fe_face_neighbor.reinit(neighbor_cell, face_no_neighbor);

            for (unsigned int i = 0; i < n_dofs; ++i)
              {
                coeffs_re = 0;
                coeffs_be = 0;

                fe_face_lift.reinit(cell_lift, face_no);

                local_rhs_re = 0;
                for (unsigned int q = 0; q < n_q_points_face; ++q)
                  {
                    const double         dx = fe_face_lift.JxW(q);
                    const Tensor<1, dim> normal =
                      fe_face_neighbor.normal_vector(q);

                    for (unsigned int m = 0; m < n_dofs_lift; ++m)
                      {
                        local_rhs_re(m) +=
                          0.5 * (fe_face_lift[tau_ext].value(m, q) * normal) *
                          fe_face_neighbor.shape_grad(i, q) * dx;
                      }
                  }

                local_rhs_be = 0;
                for (unsigned int q = 0; q < n_q_points_face; ++q)
                  {
                    const double         dx = fe_face_lift.JxW(q);
                    const Tensor<1, dim> normal =
                      fe_face_neighbor.normal_vector(q);

                    for (unsigned int m = 0; m < n_dofs_lift; ++m)
                      {
                        local_rhs_be(m) +=
                          0.5 * fe_face_lift[tau_ext].divergence(m, q) *
                          normal * fe_face_neighbor.shape_value(i, q) * dx;
                      }
                  }

                solver.solve(local_matrix_lift,
                             coeffs_re,
                             local_rhs_re,
                             PreconditionIdentity());
                solver.solve(local_matrix_lift,
                             coeffs_be,
                             local_rhs_be,
                             PreconditionIdentity());

                for (unsigned int q = 0; q < n_q_points; ++q)
                  {
                    for (unsigned int m = 0; m < n_dofs_lift; ++m)
                      {
                        discrete_hessians_neigh[face_no][i][q] -=
                          coeffs_re[m] * fe_values_lift[tau_ext].value(m, q);
                      }

                    for (unsigned int m = 0; m < n_dofs_lift; ++m)
                      {
                        discrete_hessians_neigh[face_no][i][q] +=
                          coeffs_be[m] * fe_values_lift[tau_ext].value(m, q);
                      }
                  }

              } // for dof i
          }     // boundary check
      }         // for face
  }



  template <int dim>
  void BiLaplacianLDGLift<dim>::run()
  {
    make_grid();

    setup_system();
    assemble_system();

    solve();

    compute_errors();
    output_results();
  }

} // namespace Step82



int main()
{
  try
    {
      const unsigned int n_ref = 3; // number of mesh refinements

      const unsigned int degree =
        2; // FE degree for u_h and the two lifting terms

      const double penalty_grad =
        1.0; // penalty coefficient for the jump of the gradients
      const double penalty_val =
        1.0; // penalty coefficient for the jump of the values

      Step82::BiLaplacianLDGLift<2> problem(n_ref,
                                            degree,
                                            penalty_grad,
                                            penalty_val);

      problem.run();
    }
  catch (std::exception &exc)
    {
      std::cerr << std::endl
                << std::endl
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
      std::cerr << std::endl
                << std::endl
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
