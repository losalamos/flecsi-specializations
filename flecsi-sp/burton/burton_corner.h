/*~--------------------------------------------------------------------------~*
 * Copyright (c) 2016 Los Alamos National Laboratory, LLC
 * All rights reserved
 *~--------------------------------------------------------------------------~*/
////////////////////////////////////////////////////////////////////////////////
/// \file
/// \brief This file defines the corner entity for the burton mesh.
////////////////////////////////////////////////////////////////////////////////

#pragma once

// user includes
#include <flecsi-sp/burton/burton_vertex.h>
#include <flecsi-sp/burton/burton_element.h>
#include <ristra/math/general.h>


namespace flecsi_sp {
namespace burton {

////////////////////////////////////////////////////////////////////////////////
//! \brief The burton_corner_t type provides an interface for managing and
//!   geometry and state associated with mesh corners.
//!
//! \tparam N The domain of the corner.
////////////////////////////////////////////////////////////////////////////////
template< std::size_t N >
class burton_corner_t
  : public flecsi::topology::mesh_entity__<0, burton_config_t<N>::num_domains>
{
public:

  //============================================================================
  // Typedefs
  //============================================================================

  //! the mesh traits
  using config_t = burton_config_t<N>;

  //! Number of domains in the burton mesh.
  static constexpr auto num_domains = config_t::num_domains;

  //! Number of domains in the burton mesh.
  static constexpr auto num_dimensions = config_t::num_dimensions;

  //! The domain of the entity
  static constexpr auto domain = 1;

  //! the flecsi mesh topology storage type
  using mesh_storage_t = typename config_t::mesh_storage_t;
  //! the flecsi mesh topology type
  using mesh_topology_base_t = 
    flecsi::topology::mesh_topology_base__< mesh_storage_t >;

  //============================================================================
  // Constructors
  //============================================================================

  //! default constructor
  burton_corner_t() = default;

  // dissallow copying
  burton_corner_t( burton_corner_t & ) = delete;
  burton_corner_t & operator=( burton_corner_t & ) = delete;

  // dissallow moving
  burton_corner_t( burton_corner_t && ) = delete;
  burton_corner_t & operator=( burton_corner_t && ) = delete;

};

////////////////////////////////////////////////////////////////////////////////
//! \brief The burton_corner_t type provides an interface for managing and
//!   geometry and state associated with mesh corners.
//!   This is the special 1D version.  Since corners in 1D also serve
//!   as wedges, we give this class some trivial wedge-like methods.
//!
//! \tparam N The domain of the corner.
////////////////////////////////////////////////////////////////////////////////
template<>
class burton_corner_t<1>
  : public flecsi::topology::mesh_entity__<0, burton_config_t<1>::num_domains>
{
public:

  //============================================================================
  // Typedefs
  //============================================================================

  //! the mesh traits
  using config_t = burton_config_t<1>;

  //! Number of domains in the burton mesh.
  static constexpr auto num_domains = config_t::num_domains;

  //! Number of domains in the burton mesh.
  static constexpr auto num_dimensions = config_t::num_dimensions;

  //! The domain of the entity
  static constexpr auto domain = 1;

  //! the flecsi mesh topology storage type
  using mesh_storage_t = typename config_t::mesh_storage_t;
  //! the flecsi mesh topology type
  using mesh_topology_base_t =
    flecsi::topology::mesh_topology_base__< mesh_storage_t >;

  //! The bitfield.
  using bitfield_t = typename config_t::bitfield_t;

  //! Physics vector type.
  using vector_t = typename config_t::vector_t;

  //! Coordinate point type.
  using point_t = typename config_t::point_t;

  //! the base vertex type
  using vertex_t = burton_vertex_t<num_dimensions>;

  //! the base edge type
  using edge_t = burton_edge_t<num_dimensions>;

  //! the base edge type
  using face_t = burton_face_t<num_dimensions>;

  //! the base cell type
  using cell_t = burton_cell_t<num_dimensions>;

  //============================================================================
  // Constructors
  //============================================================================

  //! default constructor
  burton_corner_t() = default;

  // dissallow copying
  burton_corner_t( burton_corner_t & ) = delete;
  burton_corner_t & operator=( burton_corner_t & ) = delete;

  // dissallow moving
  burton_corner_t( burton_corner_t && ) = delete;
  burton_corner_t & operator=( burton_corner_t && ) = delete;

  //============================================================================
  // Accessors / Modifiers for 1D wedge
  //============================================================================

  //! \brief update the wedge geometry (in 1D, a no-op)
  //! \param [in] is_right  Unused in 1D.
  template< typename MESH_TOPOLOGY >
  void update( const MESH_TOPOLOGY * mesh, bool is_right );

  //! \brief Get the cell facet normal for the wedge.
  //! \return Cell facet normal vector.
  const auto & facet_normal() const
  { return facet_normal_; }

  //! \brief the facet area
  auto facet_area() const
  { return 1.; }

  //! \brief Get the cell facet centroid
  const auto & facet_centroid() const
  { return facet_centroid_; }

  //! \brief Get the cell facet midpoint
  const auto & facet_midpoint() const
  { return facet_centroid_; }

  //! return the bitfield flags
  const auto & flags() const { return flags_; }
  auto & flags() { return flags_; }

  //! \brief Is this wedge on the boundary.
  //! \return true if on boundary.
  auto is_boundary() const
  { return flags_.test( config_t::bits::boundary ); }

  //! \brief set whether this element is on the boundary
  //! \param [in] is_boundary  True if on the boundary.
  void set_boundary( bool is_boundary )
  { flags_.set(config_t::bits::boundary, is_boundary); }

  //============================================================================
  // Private Data
  //============================================================================

private:

  //! "centroid" of the outer facet
  point_t facet_centroid_ = 0;
  //! the "normal" of the outer facet
  vector_t facet_normal_ = 0;
  //! the entity flags
  bitfield_t flags_;

};

////////////////////////////////////////////////////////////////////////////////
// facet normal for 1d wedge
////////////////////////////////////////////////////////////////////////////////
template< typename MESH_TOPOLOGY >
void burton_corner_t<1>::update( const MESH_TOPOLOGY * mesh, bool is_right )
{
  // TODO:  This code recomputes the orientation on every update -
  //        can we do it just once at initialization?
  using ristra::math::sgn;
  auto vs = mesh->template entities<vertex_t::dimension, domain, vertex_t::domain>(this);
  auto cs = mesh->template entities<cell_t::dimension, domain, cell_t::domain>(this);
  assert( vs.size() == 1 );
  const auto & v = vs.front()->coordinates();
  const auto & c = cs.front()->midpoint();
  facet_normal_ = { sgn(v[0] - c[0]) };
  facet_centroid_ = v;
}


} // namespace burton
} // namespace flecsi_sp
