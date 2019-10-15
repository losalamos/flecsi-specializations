/*~-------------------------------------------------------------------------~~*
 * Copyright (c) 2016 Los Alamos National Laboratory, LLC
 * All rights reserved
 *~-------------------------------------------------------------------------~~*/
////////////////////////////////////////////////////////////////////////////////
// \file
// \brief Tests general features of the burton mesh.
////////////////////////////////////////////////////////////////////////////////

// user includes
#include <cinchtest.h>
#include <flecsi/execution/execution.h>
#include <flecsi-sp/burton/burton_mesh.h>
#include <flecsi-sp/burton/portage_mesh_wrapper.h>
#include <flecsi-sp/burton/portage_state_wrapper.h>
#include <flecsi-sp/utils/types.h>

#include <portage/driver/mmdriver.h>

#include <portage/driver/write_to_gmv.h>
#include "tangram/driver/driver.h"
//#include "tangram/reconstruct/xmof2D_wrapper.h"
//#include "tangram/reconstruct/MOF.h"                                                             
#include "tangram/reconstruct/VOF.h"
#include "tangram/intersect/split_r3d.h"                                                         
#include "tangram/intersect/split_r2d.h"

// system includes
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

// using statements
using std::cout;
using std::endl;

using mesh_t = flecsi_sp::burton::burton_mesh_t;
using index_spaces_t = mesh_t::index_spaces_t;

using real_t = mesh_t::real_t;
using vector_t = mesh_t::vector_t;

using entity_kind_t = Portage::Entity_kind;
using entity_type_t = Portage::Entity_type;
using field_type_t = Portage::Field_type;

namespace flecsi_sp {
namespace burton {
namespace test {



////////////////////////////////////////////////////////////////////////////////
// Register the mesh state
////////////////////////////////////////////////////////////////////////////////
flecsi_register_field(
  mesh_t,      
  hydro,
  density,
  real_t,
  sparse,
  1,
  index_spaces_t::cells
);

flecsi_register_field(
  mesh_t,      
  hydro,
  velocity,
  vector_t,
  sparse,
  1,
  index_spaces_t::cells
);

flecsi_register_field(
  mesh_t,      
  hydro,
  volume_fraction,
  real_t,
  sparse,
  1,
  index_spaces_t::cells
);

flecsi_register_field(
  mesh_t,
  hydro,
  node_coordinates,
  mesh_t::vector_t,
  dense,
  1,
  index_spaces_t::vertices
);

///////////////////////////////////////////////////////////////////////////////
//! \brief Tack on an iteration number to a string
///////////////////////////////////////////////////////////////////////////////
static auto zero_padded( 
  std::size_t n, std::size_t padding = 6 
)
{
  std::stringstream ss;
  ss << std::setw( padding ) << std::setfill( '0' ) << n;
  return ss.str();
}

////////////////////////////////////////////////////////////////////////////////
/// \brief output the solution
/// \param [in] mesh  the mesh object
/// \param [in] iteration  the iteration count
/// \param [in] d  the bulk density
////////////////////////////////////////////////////////////////////////////////
void output( 
  utils::client_handle_r<mesh_t> mesh, 
  size_t iteration,
  utils::sparse_handle_r<real_t> d,
  utils::sparse_handle_r<real_t> vf
) {
  clog(info) << "OUTPUT MESH TASK" << std::endl;

  // get the context
  auto & context = flecsi::execution::context_t::instance();
  auto rank = context.color();
  auto size = context.colors();
  auto time = 0.0;

  constexpr auto num_dims = mesh_t::num_dimensions;

  // figure out this ranks file name
  auto output_filename = "burton.remap_test"
    + zero_padded(iteration) + "_rank"
    + zero_padded(rank) + ".vtk";

  std::ofstream file(output_filename);

  file << "# vtk DataFile Version 3.0" << std::endl;
  file << "Remapping test" << std::endl;
  file << "ASCII" << std::endl;
  file << "DATASET UNSTRUCTURED_GRID" << std::endl;
 
  file.precision(14);
  file.setf( std::ios::scientific );

  file << "POINTS " << mesh.num_vertices() << " double" << std::endl;
  for (auto v : mesh.vertices()) {
    for (int d=0; d<num_dims; d++ ) 
      file << v->coordinates()[d] << " ";
    for (int d=num_dims; d<3; d++ )
      file << 0 << " ";
    file << std::endl;
  }

  size_t vert_cnt = 0;
  const auto & cells = mesh.cells();
  for ( auto c : cells ) vert_cnt += mesh.vertices(c).size() + 1;

  file << "CELLS " << cells.size() << " " << vert_cnt << std::endl;    
  for ( auto c : cells ) {
    const auto & vs = mesh.vertices(c);
    file << vs.size() << " ";
    for ( auto v : vs )
      file << v.id() << " ";
    file << std::endl;
  }

  file << "CELL_TYPES " << cells.size() << std::endl;
  for ( auto c : cells ) file << "7" << std::endl;
    
  file << "CELL_DATA " << cells.size() << std::endl;    
  file << "SCALARS density double 1" << std::endl;
  file << "LOOKUP_TABLE default" << std::endl;
  
  for ( auto c : cells ) {
    auto bulk = 0.0;
    for (auto m: d.entries(c)){
      //      bulk += d(c,m);
      bulk += d(c,m) * vf(c,m);
      //      file << d(c,m) << std::endl;
    }
    file << bulk << std::endl;
  }

  file.close();

}

////////////////////////////////////////////////////////////////////////////////
/// \brief make a remapper object
////////////////////////////////////////////////////////////////////////////////
template<
  typename mesh_wrapper_a_t,
  typename state_wrapper_a_t,
  typename mesh_wrapper_b_t,
  typename state_wrapper_b_t,
  typename var_name_t
  >
auto make_remapper(
         mesh_wrapper_a_t & mesh_wrapper_a,
         state_wrapper_a_t & state_wrapper_a,
         mesh_wrapper_b_t & mesh_wrapper_b,
         state_wrapper_b_t & state_wrapper_b,
         var_name_t & var_names
         ) {

  // std::cout << " Inside make_remapper" << std::endl;
  // for ( auto v: var_names) {
  //   state_wrapper_a.check_map(v);
  //   state_wrapper_b.check_map(v);
  // }
  // std::cout << " Inside make_remapper" << std::endl;

  constexpr int dim = mesh_wrapper_a_t::mesh_t::num_dimensions;
  if constexpr( dim == 2) {
    Portage::MMDriver<
      Portage::SearchKDTree,
      Portage::IntersectR2D,
      Portage::Interpolate_2ndOrder,
      mesh_t::num_dimensions,
      portage_mesh_wrapper_t<mesh_t>,
      portage_state_wrapper_t<mesh_t>,
      portage_mesh_wrapper_t<mesh_t>,
      portage_state_wrapper_t<mesh_t>,
      Tangram::VOF,
      Tangram::SplitR2D,
      Tangram::ClipR2D > remapper(
                mesh_wrapper_a,
                state_wrapper_a,
                mesh_wrapper_b,
                state_wrapper_b);
    return std::move(remapper);
  } else if (dim == 3) {
    Portage::MMDriver<
      Portage::SearchKDTree,
      Portage::IntersectR3D,
      Portage::Interpolate_1stOrder,
      mesh_t::num_dimensions,
      portage_mesh_wrapper_t<mesh_t>,
      portage_state_wrapper_t<mesh_t>,
      portage_mesh_wrapper_t<mesh_t>,
      portage_state_wrapper_t<mesh_t>,
      Tangram::VOF,
      Tangram::SplitR3D,
      Tangram::ClipR3D > remapper(
                mesh_wrapper_a,
                state_wrapper_a,
                mesh_wrapper_b,
                state_wrapper_b);
    return std::move(remapper);
  } else {
    static_assert(dim!=3 || dim!=2, "Make_remapper dimensions are out of range");
  }
}

template<
  typename mesh_wrapper_a_t,
  typename tolerances_t
  >
auto make_interface_reconstructor(
         mesh_wrapper_a_t & mesh_wrapper_a,
         tolerances_t & tols,
         bool & all_convex
         ) {

  using mesh_wrapper_t = flecsi_sp::burton::portage_mesh_wrapper_t<mesh_t>;

  constexpr int num_dims = mesh_wrapper_a_t::mesh_t::num_dimensions;
  if constexpr (num_dims == 2){
    auto interface_reconstructor = std::make_shared<Tangram::Driver<Tangram::VOF, num_dims, 
                                          mesh_wrapper_t,Tangram::SplitR2D,Tangram::ClipR2D> 
					  >(mesh_wrapper_a, tols, all_convex);
    return std::move(interface_reconstructor);
  } else if (num_dims == 3) {
    auto interface_reconstructor = std::make_shared<Tangram::Driver<Tangram::VOF, num_dims, 
                                          mesh_wrapper_t,Tangram::SplitR3D,Tangram::ClipR3D> 
					  >(mesh_wrapper_a, tols, all_convex);
    return std::move(interface_reconstructor);
  } else {
    static_assert(num_dims != 3 || num_dims !=2 , 
                  "make_interface_reconstructor dimensions are out of range");
  }
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Test the remap capabilities of portage + tangram
/// \param [in] mesh       the mesh object
/// \param [in] mat_state  a densely populated set of data
/// \param [in] coord0     the set of coordinates to be applied
////////////////////////////////////////////////////////////////////////////////
void remap_tangram_test(
  utils::client_handle_r<mesh_t> mesh,
  utils::sparse_handle_rw<real_t> density_handle,
  utils::sparse_handle_rw<real_t> volfrac_handle,
  utils::sparse_handle_rw<vector_t> velocity_handle,
  utils::sparse_mutator<real_t> density_mutator,
  utils::sparse_mutator<real_t> volfrac_mutator,
  utils::sparse_mutator<vector_t> velocity_mutator,
  utils::dense_handle_r<vector_t> new_vertex_coords
) {
  
  using mesh_wrapper_t = flecsi_sp::burton::portage_mesh_wrapper_t<mesh_t>;

  constexpr auto num_dims = mesh_t::num_dimensions;
  auto num_mats = 2; //(volfrac_handle.max_entries()) // WHY IS THIS 5?;
  constexpr auto epsilon = config::test_tolerance;

  // some mesh parameters
  const auto & cells = mesh.cells();
  const auto & owned_cells = mesh.cells(flecsi::owned);
  auto num_cells = cells.size();
  auto num_owned_cells = owned_cells.size();

  // Lambda Functions to simplify indexing 
  auto scalar_index = [=](auto c, auto m) {
    return c + m * num_cells;
  };

  auto vector_index = [=](auto c, auto m, auto d) {
    return c + (m * num_dims + d) * num_cells;
  };

  // get the context
  auto & context = flecsi::execution::context_t::instance();
  auto comm_rank = context.color();
  auto comm_size = context.colors();

  //---------------------------------------------------------------------------
  // Compute some source and target mesh quantities.

  // Apply the new coordinates to the mesh and update its geometry.
  // Here we save some info we need.
  std::vector< real_t > target_cell_volume(mesh.num_cells());
  std::vector< vector_t > target_cell_centroid(mesh.num_cells());
  // update coordinates
  for ( auto v : mesh.vertices() )
    std::swap(v->coordinates(), new_vertex_coords(v));
  // compute new target mesh quantities
  for ( auto c: mesh.cells() ) {
    c->update(&mesh);
    target_cell_volume[c] = c->volume();
    target_cell_centroid[c] = c->centroid();
  }

  // Store the cell volumes for the source mesh 
  std::vector< real_t > source_volume(mesh.num_cells());
  // update coordinates back to old mesh
  for ( auto v : mesh.vertices() )
    std::swap(v->coordinates(), new_vertex_coords(v));
  // re-compute source mesh quantities
  for ( auto c: mesh.cells() ) {
    c->update(&mesh);
    source_volume[c] = c->volume();
  }

  //---------------------------------------------------------------------------
  // Set up portage mesh/data wrappers

  // Create the mesh wrapper objects
  portage_mesh_wrapper_t<mesh_t> source_mesh_wrapper(mesh);
  portage_mesh_wrapper_t<mesh_t> target_mesh_wrapper(mesh);

  target_mesh_wrapper.set_new_coordinates(
    &new_vertex_coords(0),
    target_cell_volume.data(),
    target_cell_centroid.data() );

  // Create the state wrapper objects
  portage_state_wrapper_t<mesh_t> source_state_wrapper(mesh);
  portage_state_wrapper_t<mesh_t> target_state_wrapper(mesh);

  // Create a vector of strings that correspond to the names of the variables
  //   that will be remapped
  static const char coordinate[] = {'x', 'y', 'z'};    
  std::vector<std::string> var_names_all;
  std::vector<std::string> meshvar_names;
  std::vector<std::string> matvar_names;

  // default limiter and map
  std::map<std::string, Portage::Limiter_type > limiter_map;
  auto limiter = Portage::Limiter_type::NOLIMITER;

  source_state_wrapper.num_materials(num_mats);
  target_state_wrapper.num_materials(num_mats);

  auto vec_len = num_mats * num_cells;

  // Create temporary vectors for the data to be remapped
  std::vector< double > density;
  std::vector< double > remap_density(vec_len, 0);

  std::vector< double > volfrac;
  std::vector< double > remap_volfrac(vec_len, 0);

  std::vector< double > velocity;
  std::vector< double > remap_velocity(vec_len*num_dims, 0);

  std::vector< std::vector< int > > mat_cells(num_mats);
  std::vector< std::vector< int > > cell_mats(num_cells);
  std::vector< int > num_mats_cell(num_cells);
  std::vector< int > mat_offsets{0};
  std::vector< int > vec_offsets{0};


  // Fill the temporary vectors with the data that needs to be remapped
  for (int m=0; m< num_mats; ++m){
    for (auto c: velocity_handle.indices(m) ){
      mat_cells[m].push_back(c);
      cell_mats[c].push_back(m);
      volfrac.push_back(volfrac_handle(c,m));
      density.push_back(density_handle(c,m));
    }
    mat_offsets.push_back(density.size());
  }

  for (int dim=0; dim < num_dims; ++dim){
    for (int m=0; m < num_mats; ++m){
      for (auto c: velocity_handle.indices(m)){
        velocity.push_back(velocity_handle(c,m)[dim]);
      }
    }
    vec_offsets.push_back(velocity.size());
  }

  // Initialize material offsets and cells in wrappers
  source_state_wrapper.set_mat_offsets(mat_offsets);

  source_state_wrapper.mat_set_cells(mat_cells);
  source_state_wrapper.cell_set_mats(cell_mats);
    // Initialization with source data, but will be reset to target
    // data when it is known by portage/tangram
  target_state_wrapper.mat_set_cells(mat_cells);
  target_state_wrapper.cell_set_mats(cell_mats);

  // for (auto m=0; m < num_mats; ++m) {
  //   auto mat_name = "mat_" + std::to_string(m);
  //   source_state_wrapper.add_material( mat_name, mat_cells[m] );
  //   source_state_wrapper.check_mat_map(mat_name);
  // }
  target_state_wrapper.num_materials(num_mats);

  // Initialization of source state
  std::vector<int> offsets(num_cells+1,0);
  // count first, then turn into offsets
  for (auto c: mesh.cells()){
    offsets[c.id()+1] += volfrac_handle.entries(c).size();
  }
  for (size_t i=0; i<num_cells; ++i){
    offsets[i+1] += offsets[i];
  }
  std::vector<int> mat_ids( offsets.back() );
  std::vector<int> index_vals( offsets.back() );
  std::vector<int> cell_counter(num_cells,0);

  for (int m=0; m<num_mats; ++m){
    size_t mat_cell_id = 0;
    for (auto c: velocity_handle.indices(m)){

      // add to list
      auto offset = offsets[c] + cell_counter[c];
      index_vals[offset] = mat_cell_id;
      mat_ids[offset] = m;

      //bump counters
      cell_counter[c]++;
      mat_cell_id++;
    }
  }

  source_state_wrapper.add_offsets(offsets);
  source_state_wrapper.add_mat_ids(mat_ids);
  source_state_wrapper.add_index_vals(index_vals);

  for (int m=0; m < num_mats; ++m){
    auto volfrac_name = "mat_volfracs" + std::to_string(m);
    source_state_wrapper.add_cell_field(volfrac_name, 
                                        volfrac.data() + mat_offsets[m]);
  }

  auto density_name = "density";
  source_state_wrapper.add_cell_field( density_name, density.data(),
                                       entity_kind_t::CELL,
                                       field_type_t::MULTIMATERIAL_FIELD);
  var_names_all.push_back(density_name);
  matvar_names.push_back(density_name);

  for ( int dim=0; dim<num_dims; ++dim ) {
    std::string vel_name = "vel_";
    vel_name += coordinate[dim];
    var_names_all.emplace_back(vel_name);
    matvar_names.push_back(vel_name);
    source_state_wrapper.add_cell_field( vel_name, velocity.data() + vec_offsets[dim],
                                       entity_kind_t::CELL,
                                       field_type_t::MULTIMATERIAL_FIELD);
  }
 
  limiter_map[var_names_all.back()] = limiter;
  
  // Initialization of target state
  for (int m=0; m < num_mats; ++m){
    auto scalar_offset = scalar_index(0,m);
    auto volfrac_name = "mat_volfracs" + std::to_string(m);
    target_state_wrapper.add_cell_field(volfrac_name, 
                                        remap_volfrac.data() + scalar_offset);    
    
    auto density_name = "density" + std::to_string(m);
    target_state_wrapper.add_cell_field(density_name,
                                        remap_density.data() + scalar_offset, 
                                        entity_kind_t::CELL,
                                        field_type_t::MULTIMATERIAL_FIELD);

    for ( int dim=0; dim<num_dims; ++dim ) {
      auto dim_offset = vector_index(0,m,dim);
      std::string vel_name = "vel_";
      vel_name += coordinate[dim] + std::to_string(m);
      target_state_wrapper.add_cell_field(vel_name,
                                          remap_velocity.data() + dim_offset, 
                                          entity_kind_t::CELL,
                                          field_type_t::MULTIMATERIAL_FIELD);
  
    }
    density_name = "density_test" + std::to_string(m);
    target_state_wrapper.add_cell_field(density_name,
                                        remap_density.data() + scalar_offset, 
                                        entity_kind_t::CELL,
                                        field_type_t::MULTIMATERIAL_FIELD);

    for ( int dim=0; dim<num_dims; ++dim ) {
      auto dim_offset = vector_index(0,m,dim);
      std::string vel_name = "vel_test";
      vel_name += coordinate[dim] + std::to_string(m);
      target_state_wrapper.add_cell_field(vel_name,
                                          remap_velocity.data() + dim_offset, 
                                          entity_kind_t::CELL,
                                          field_type_t::MULTIMATERIAL_FIELD);
  
    }
  }

  // std::cout << " Before make_remapper" << std::endl;
  // for ( auto v: var_names_all) {
  //   source_state_wrapper.check_map(v);
  //   target_state_wrapper.check_map(v);
  // }
  // std::cout << " Before make_remapper" << std::endl;

  // Initializations for Tangram
  auto mpi_comm = MPI_COMM_WORLD;
  Wonton::MPIExecutor_type mpiexecutor(mpi_comm);
  bool all_convex = false;
  std::vector<Tangram::IterativeMethodTolerances_t> tols(2,{1000, 1e-15, 1e-15});
  auto source_interface_reconstructor = make_interface_reconstructor(
                                          source_mesh_wrapper,
                                          tols,
                                          all_convex);

  std::vector<int> cell_num_mats;
  std::vector<int> cell_mat_ids;
  std::vector<real_t> cell_mat_volfracs;
  for (auto c:cells) {
    cell_num_mats.push_back((volfrac_handle.entries(c)).size());
    for (auto m: volfrac_handle.entries(c)) {
      cell_mat_ids.push_back(m);
      cell_mat_volfracs.push_back(volfrac_handle(c,m));
    }
  }

  source_interface_reconstructor->set_volume_fractions(cell_num_mats,
                                                      cell_mat_ids,
                                                      cell_mat_volfracs);
  source_interface_reconstructor->reconstruct(&mpiexecutor);

  // Build remapper
  auto remapper = make_remapper(
             source_mesh_wrapper,
             source_state_wrapper,
             target_mesh_wrapper,
             target_state_wrapper,
             matvar_names);

  // Assign the remap varaible names for the portage driver
  remapper.set_remap_var_names(var_names_all);
  remapper.set_limiter( Portage::Limiter_type::BARTH_JESPERSEN );
  //remapper.set_limiter( Portage::Limiter_type::NOLIMITER );

  // Tangram necessity
  //remapper.set_reconstructor_options(tols,all_convex);

  // std::cout << " After make_remapper" << std::endl;
  // for ( auto v: var_names_all) {
  //   source_state_wrapper.check_map(v);
  //   target_state_wrapper.check_map(v);
  // }
  // std::cout << " After make_remapper" << std::endl;

  // Do the remap 
  // Argument is boolean: distributed (true or false)
  remapper.template remap<portage_mesh_wrapper_t<mesh_t>,
			  portage_state_wrapper_t<mesh_t>, Portage::Entity_kind::CELL>(
                                source_mesh_wrapper, source_state_wrapper,
                                              meshvar_names, meshvar_names,
                                              matvar_names, matvar_names, &mpiexecutor);

  //---------------------------------------------------------------------------
  // Swap old for new data

  // Checking conservation for remap test
  real_t total_density{0};
  vector_t total_velocity{0};
  real_t total_volume{0};

  // Calculate totals for old data
  for (auto c: mesh.cells(flecsi::owned)) {
    for ( auto m : velocity_handle.entries(c) ) {
      auto offset = scalar_index(c,m);
      total_density +=  c->volume() * volfrac_mutator(c,m) *  density_handle(c,m);
      total_volume +=  c->volume() * volfrac_mutator(c,m);
      auto & vel = velocity_handle(c,m);
      for (int dim=0; dim < num_dims; ++dim) {
        total_velocity[dim] += c->volume() * volfrac_mutator(c,m) * vel[dim];
      }
    }
  }

  // Swap old data for new data
  for (int mat_num=0; mat_num < num_mats; ++mat_num){
    mat_cells[mat_num].clear();
    target_state_wrapper.mat_get_cells(mat_num,  &mat_cells[mat_num]);
    double *mat_volfrac;
    target_state_wrapper.mat_get_volfracs(mat_num, &mat_volfrac);
    for (int count=0; count<mat_cells[mat_num].size(); ++count){
      auto element = mat_cells[mat_num][count];
      density_mutator(element, mat_num) = remap_density[count + scalar_index(0,mat_num)];
      volfrac_mutator(element, mat_num) = mat_volfrac[count]; 
      auto vel = velocity_mutator(0,0);
      for (int dim=0; dim < num_dims; ++dim) {
        auto dim_offset = vector_index(count, mat_num, dim);
        vel[dim] = remap_velocity[dim_offset];
      }
      velocity_mutator(element, mat_num) = vel;
    }
  }

  // Apply the new coordinates to the mesh and update its geometry
  for ( auto v : mesh.vertices() ) v->coordinates() = new_vertex_coords(v);
  mesh.update_geometry();
  
  //---------------------------------------------------------------------------
  // Post process

  // Check conservation for remap test
  real_t total_remap_density{0};
  vector_t total_remap_velocity{0};
  real_t total_remap_volume{0};
  // Calculate the L1 and L2 norms
  real_t total_vol = 0.0;
  real_t L1 = 0.0;
  real_t L2 = 0.0;
  
  std::vector<double> vfrac_sum(num_cells, 0.0);

  std::vector<real_t> actual(num_cells, 0.0);
  for (int mat_num=0; mat_num < num_mats; ++mat_num){
    auto cells = mesh.cells(flecsi::owned);
    for (int element: mat_cells[mat_num]){
      auto c = cells[element];
      total_vol += c->volume() * volfrac_mutator(c,mat_num);
      actual[c.id()] += volfrac_mutator(c,mat_num) * density_mutator(c,mat_num);
      total_remap_density += c->volume() * volfrac_mutator(c,mat_num) * density_mutator(c,mat_num);
      vfrac_sum[c.id()] += volfrac_mutator(c,mat_num);
      total_remap_volume += c->volume() * volfrac_mutator(c,mat_num);
      const auto & vel = velocity_mutator(c,mat_num);
      for (int dim=0; dim<num_dims; ++dim) {
        total_remap_velocity[dim] += c->volume() * volfrac_mutator(c,mat_num) * vel[dim];
      }
    }
  }
  
  for (auto c: mesh.cells(flecsi::owned)){
    real_t sum{0}, expected;
  #ifdef CUBIC
    for ( int i=0; i < num_dims; ++i)
      sum += ((c->centroid())[i]+0.5);
    expected = pow(sum,3.0);
  #elif LINEAR
    for ( int i=0; i < num_dims; ++i)
      sum += ((c->centroid())[i]+0.5);
    expected = sum * 2.0;
  #elif COSINE
    real_t arg = 10.0 * std::sqrt( pow((c->centroid())[0], 2) + pow((c->centroid())[1], 2) );
    expected = 1.0 + (std::cos(arg) / 3.0);
  #else
    expected = 1.0;
    //EXPECT_NEAR(expected, actual[c.id()], epsilon*10);
  #endif
  
    // L1,L2 errors
    auto each_error = std::abs(actual[c.id()] - expected);
    L1 +=  c->volume() * each_error; // L1
    L2 +=  c->volume() * std::pow(each_error, 2); // L2    
  }

  // output L1, L2 errors for debug
  real_t total_vol_sum{0}, L1_sum{0}, L2_sum{0};
  int ret;
  ret = MPI_Allreduce( &total_vol, &total_vol_sum, 1, MPI_DOUBLE, MPI_SUM, mpi_comm);
  ret = MPI_Allreduce( &L1, &L1_sum, 1, MPI_DOUBLE, MPI_SUM, mpi_comm);
  ret = MPI_Allreduce( &L2, &L2_sum, 1, MPI_DOUBLE, MPI_SUM, mpi_comm);
  L1_sum = L1_sum/total_vol_sum; // L1
  L2_sum = pow(L2_sum/total_vol_sum, 0.5); // L2

  if ( comm_rank == 0 ) {
    printf("L1 Norm: %.20f, Total Volume: %f \n", L1_sum, total_vol_sum);
    printf("L2 Norm: %.20f, Total Volume: %f \n", L2_sum, total_vol_sum);
    EXPECT_NEAR(L1_sum, 0.0, epsilon);
    EXPECT_NEAR(L2_sum, 0.0, epsilon);
  }

  // Verify conservation
  real_t total_density_sum{0}, total_remap_density_sum{0};
  vector_t total_velocity_sum{0}, total_remap_velocity_sum{0};
  ret = MPI_Allreduce( &total_density, &total_density_sum, 1, MPI_DOUBLE, 
                       MPI_SUM, mpi_comm);
  ret = MPI_Allreduce( &total_remap_density, &total_remap_density_sum, 1, 
                       MPI_DOUBLE, MPI_SUM, mpi_comm);
  ret = MPI_Allreduce( total_velocity.data(), total_velocity_sum.data(), 
                       total_velocity.size(), MPI_DOUBLE, MPI_SUM, mpi_comm);
  ret = MPI_Allreduce( total_remap_velocity.data(), total_remap_velocity_sum.data(), 
                       total_remap_velocity.size(), MPI_DOUBLE, MPI_SUM, mpi_comm);

  EXPECT_NEAR( total_density_sum, total_remap_density_sum, epsilon);
  for ( int i=0; i<num_dims; ++i)
    EXPECT_NEAR( total_velocity_sum[i], total_remap_velocity_sum[i], epsilon);
}


////////////////////////////////////////////////////////////////////////////////
/// \brief Test the remap capabilities of portage
/// \param [in] mesh       the mesh object
/// \param [in] mat_state  a densely populated set of data
////////////////////////////////////////////////////////////////////////////////
void initialize_flat(
  utils::client_handle_r<mesh_t> mesh,
  utils::sparse_mutator<real_t> density,
  utils::sparse_mutator<real_t> vol_frac,
  utils::sparse_mutator<vector_t> velocity
) {
  int m = 0;
  float num_cells_x = 32.0;
  for (auto c: mesh.cells(flecsi::owned)){
    c->update(&mesh);
    auto centroid_x = (c->centroid())[0];
    if (centroid_x < -6.05/num_cells_x){
      m = 0;
      vol_frac(c,m) = 1.0;
      density(c,m) = 1.0;
      velocity(c,m) = 1.0;
    } else if (centroid_x > -4.95/num_cells_x){
      m = 1;
      vol_frac(c,m) = 1.0;
      density(c,m) = 1.0;
      velocity(c,m) = 1.0;
    } else {
      for ( int i=0; i < 2; ++i ){
        vol_frac(c,i) = 0.5;
        density(c,i) = 2.0;
        velocity(c,i) = 2.0;
      }
    }
  }
}
 
void initialize_linear(
  utils::client_handle_r<mesh_t> mesh,
  utils::sparse_mutator<real_t> density,
  utils::sparse_mutator<real_t> vol_frac,
  utils::sparse_mutator<vector_t> velocity
) {

  real_t val;
  int m = 0;
  float num_cells_x = 32.0;
  for (auto c: mesh.cells(flecsi::owned)){
    c->update(&mesh);
    auto centroid_x = c->centroid()[0];
    real_t sum = 0.0;
    // for ( int i=0; i < mesh_t::num_dimensions; i++)
    //   sum += ((c->centroid())[i]+0.5);
    sum = ((c->centroid())[0]+0.5);
    val = sum * 1.0;
    if (centroid_x < -6.05/num_cells_x){
      m = 0;
      density(c,m) = val;
      vol_frac(c,m) = 1.0;
      velocity(c,m) = val;
    } else if (centroid_x > -4.95/num_cells_x){
      m = 1;
      density(c,m) = val;
      vol_frac(c,m) = 1.0;
      velocity(c,m) = val;
    } else {
      for (int m=0; m<2; ++m){
        density(c,m) = val;
        vol_frac(c,m) = 0.5;
        velocity(c,m) = val;
      }
    }
  }
}

void initialize_cubic(
  utils::client_handle_r<mesh_t> mesh,
  utils::sparse_mutator<real_t> density,
  utils::sparse_mutator<real_t> vol_frac,
  utils::sparse_mutator<vector_t> velocity
) {

  int m = 0;
  int num_cells_x = 32;
  int split = floor(num_cells_x/2);
  for (auto c: mesh.cells(flecsi::owned)){
    real_t sum = 0.0;
    for ( int i=0; i < mesh_t::num_dimensions; i++)
      sum += pow((c->centroid())[i]+0.5, 3.0);
    auto val = sum;
    if (c.id()%num_cells_x < split){
      m = 0;
      density(c,m) = val;
      vol_frac(c,m) = 1.0;
      velocity(c,m) = val;
    } else if (c.id()%num_cells_x > split) {
      m = 1;
      density(c,m) = val;
      vol_frac(c,m) = 1.0;
      velocity(c,m) = val;
    } else {
      for (int m=0; m<2; ++m){
        density(c,m) = val;
        vol_frac(c,m) = 0.5;
        velocity(c,m) = val;
      }
    }
  }
}

void initialize_cosine(
  utils::client_handle_r<mesh_t> mesh,
  utils::sparse_mutator<real_t> density,
  utils::sparse_mutator<real_t> vol_frac,
  utils::sparse_mutator<vector_t> velocity
) {

  int m = 0;
  int num_cells_x = 32;
  int split = floor(num_cells_x/2);
  for (auto c: mesh.cells(flecsi::owned)){
    real_t sum = 0.0;
    for ( int i=0; i < mesh_t::num_dimensions; i++)
      sum += pow((c->centroid())[i], 2);
    auto arg = 10.0 * std::sqrt( sum );
    auto val = 1.0 + (std::cos(arg) / 3.0);
    if (c.id()%num_cells_x < split){
      m = 0;
      density(c,m) = val;
      vol_frac(c,m) = 1.0;
      velocity(c,m) = val;
    } else if (c.id()%num_cells_x > split) {
      m = 1;
      density(c,m) = val;
      vol_frac(c,m) = 1.0;
      velocity(c,m) = val;
    } else {
      for (int m=0; m<2; ++m){
        density(c,m) = val;
        vol_frac(c,m) = 0.5;
        velocity(c,m) = val;
      }
    }
  }
}

void initialize_step(
  utils::client_handle_r<mesh_t> mesh,
  utils::sparse_mutator<real_t> density,
  utils::sparse_mutator<real_t> vol_frac,
  utils::sparse_mutator<vector_t> velocity
) {

  int m = 0;
  int num_cells_x = 32;
  int split = floor(num_cells_x/2);
  for (auto c: mesh.cells(flecsi::owned)){
    real_t val;
    if (c.id()%num_cells_x < split){
      m = 0;
      density(c,m) = 0.0;
      vol_frac(c,m) = 1.0;
      velocity(c,m) = 0.0;
    } else if (c.id()%num_cells_x > split) {
      m = 1;
      density(c,m) = 1.0;
      vol_frac(c,m) = 1.0;
      velocity(c,m) = 1.0;
    } else {
      for (int m=0; m<2; ++m){
        density(c,m) = m;
        vol_frac(c,m) = 0.5;
        velocity(c,m) = m;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief modify the coordinates & solution
//!
//! \param [in] mesh the mesh object
//! \param [out] coord0  storage for the mesh coordinates
////////////////////////////////////////////////////////////////////////////////
void modify( 
  utils::client_handle_r<mesh_t>  mesh,
  utils::dense_handle_w<vector_t> coord
)
{
  constexpr auto num_dims = mesh_t::num_dimensions;

  // Loop over vertices
  const auto & vs = mesh.vertices();
  auto num_verts = vs.size();

  std::mt19937 mt_rand(0);
  auto real_rand = std::bind(
    std::uniform_real_distribution<double>(0, 0.2),
    mt_rand);

  //Randomly modifies each of the vertices such that 
  // no vertex moves by more than 40% of the spacing
  // in any direction. These vertices are then used
  // for the new mesh.

  double spacing = 1/std::pow(num_verts, 0.5);
  for (auto v : vs ) {
    auto vertex = v->coordinates();
    
    for ( int j=0; j < num_dims; j++) {
      if ( vertex[j] < 0.5 && vertex[j] > 0) {
        auto perturbation = spacing * real_rand();
	//perturbation = 0;
        vertex[j] -= perturbation;
      } else if (vertex[j] <= 0 && vertex[j]> -0.5) {
        auto perturbation = spacing * real_rand();
	//perturbation = 0;
        vertex[j] += perturbation;
      }
    }
    
    coord(v) = vertex;
  }
} 

////////////////////////////////////////////////////////////////////////////////
//! \brief restore the coordinates
//!
//! \param [in,out] mesh  the mesh object
//! \param [in] coord0  the mesh coordinates to restore
////////////////////////////////////////////////////////////////////////////////
void restore( 
  utils::client_handle_r<mesh_t>  mesh,
  utils::dense_handle_r<vector_t> coord
)
{

  // Loop over vertices
  auto vs = mesh.vertices();
  auto num_verts = vs.size();

  for ( mesh_t::counter_t i=0; i<num_verts; i++ ) {
    auto vt = vs[i];
    vt->coordinates() = coord(vt);
  }

}// TEST_F

flecsi_register_mpi_task(remap_tangram_test, flecsi_sp::burton::test);
flecsi_register_task(output, flecsi_sp::burton::test, loc,
  single|flecsi::leaf);

// Different Initialization Tasks
flecsi_register_task(initialize_flat, flecsi_sp::burton::test, loc,
  single|flecsi::leaf);
flecsi_register_task(initialize_linear, flecsi_sp::burton::test, loc,
  single|flecsi::leaf);
flecsi_register_task(initialize_cubic, flecsi_sp::burton::test, loc,
  single|flecsi::leaf);
flecsi_register_task(initialize_cosine, flecsi_sp::burton::test, loc,
  single|flecsi::leaf);
flecsi_register_task(initialize_step, flecsi_sp::burton::test, loc,
  single|flecsi::leaf);

flecsi_register_task(restore, flecsi_sp::burton::test, loc,
         single|flecsi::leaf);
flecsi_register_task(modify, flecsi_sp::burton::test, loc,
         single|flecsi::leaf);

} // namespace
} // namespace
} // namespace

namespace flecsi {
namespace execution {


////////////////////////////////////////////////////////////////////////////////
//! \brief the driver for all tests
////////////////////////////////////////////////////////////////////////////////
void driver(int argc, char ** argv)
{

  // get the mesh handle
  auto mesh_handle = flecsi_get_client_handle(mesh_t, meshes, mesh0);

  auto num_materials = 2;

  size_t time_cnt{0};

  auto xn = flecsi_get_handle(mesh_handle, hydro, node_coordinates, vector_t, dense, 0);
  auto density_handle  = flecsi_get_handle(mesh_handle, hydro, density,  real_t,   sparse, 0);
  auto volfrac_handle  = flecsi_get_handle(mesh_handle, hydro, volume_fraction, real_t,   sparse, 0);
  auto velocity_handle = flecsi_get_handle(mesh_handle, hydro, velocity, vector_t, sparse, 0);
  auto density_mutator  = flecsi_get_mutator(mesh_handle, hydro, density,  real_t,   sparse, 0, num_materials);
  auto volfrac_mutator  = flecsi_get_mutator(mesh_handle, hydro, volume_fraction,  real_t,   sparse, 0, num_materials);
  auto velocity_mutator = flecsi_get_mutator(mesh_handle, hydro, velocity, vector_t, sparse, 0, num_materials);

#ifdef CUBIC
  flecsi_execute_task(
          initialize_cubic,
          flecsi_sp::burton::test,
          single,
          mesh_handle,
          density_mutator,
          volfrac_mutator,
          velocity_mutator);
#elif LINEAR
  flecsi_execute_task(
          initialize_linear,
          flecsi_sp::burton::test,
          single,
          mesh_handle,
          density_mutator,
          volfrac_mutator,
          velocity_mutator);
#elif STEP
  flecsi_execute_task(
          initialize_step,
          flecsi_sp::burton::test,
          single,
          mesh_handle,
          density_mutator,
          volfrac_mutator,
          velocity_mutator);
#elif COSINE
  flecsi_execute_task(
          initialize_cosine,
          flecsi_sp::burton::test,
          single,
          mesh_handle,
          density_mutator,
          volfrac_mutator,
          velocity_mutator);
#else
  flecsi_execute_task(
          initialize_flat,
          flecsi_sp::burton::test,
          single,
          mesh_handle,
          density_mutator,
          volfrac_mutator,
          velocity_mutator);
#endif
  
  time_cnt++;
  flecsi_execute_task(
            output,
            flecsi_sp::burton::test,
            single,
            mesh_handle, time_cnt, density_handle, volfrac_handle);

  flecsi_execute_task(
          modify,
          flecsi_sp::burton::test,
          single,
          mesh_handle,
          xn);

#if 1
  flecsi_execute_mpi_task(
          remap_tangram_test, 
          flecsi_sp::burton::test, 
          mesh_handle,
          density_handle,
          volfrac_handle,	  
          velocity_handle,
          density_mutator,
          volfrac_mutator,
          velocity_mutator,
          xn);
#else
  flecsi_execute_task(
          restore,
          flecsi_sp::burton::test,
          single,
          mesh_handle,
          xn);
#endif

  time_cnt++;
  flecsi_execute_task(
            output,
            flecsi_sp::burton::test,
            single,
            mesh_handle, time_cnt, density_handle, volfrac_handle);

} // driver

} // namespace execution
} // namespace flecsi

////////////////////////////////////////////////////////////////////////////////
//! \brief Only here so test runs?
////////////////////////////////////////////////////////////////////////////////
TEST(burton, portage) {}

