//-------------------------------------------------------------
//    $Id$
//
//    Copyright (C) 2011 by the authors of the ASPECT code
//
//-------------------------------------------------------------
#ifndef __aspect__geometry_model_box_h
#define __aspect__geometry_model_box_h

#include <aspect/geometry_model/interface.h>


namespace aspect
{
  namespace GeometryModel
  {
    using namespace dealii;

    /**
     * A class that describes a box geometry of certain width, height, and
     * depth (in 3d).
     */
    template <int dim>
    class Box : public Interface<dim>
    {
      public:
        /**
         * Generate a coarse mesh for the geometry described by this class.
         */
        virtual
        void create_coarse_mesh (parallel::distributed::Triangulation<dim> &coarse_grid) const;

        /**
         * Return the typical length scale one would expect of features in this geometry,
         * assuming realistic parameters.
         *
         * We return 1/100th of the diameter of the box.
         */
        virtual
        double length_scale () const;


        virtual
        double depth(const Point<dim> &position) const;

        virtual
        Point<dim> representative_point(const double depth) const;

        virtual
        double maximal_depth() const;

        /**
         * Return the set of boundary indicators that are used by this model. This
         * information is used to determine what boundary indicators can be used in
         * the input file.
         *
         * The box model uses boundary indicators zero through 2*dim-1, with the first
         * two being the faces perpendicular to the x-axis, the next two perpendicular
         * to the y-axis, etc.
         */
        virtual
        std::set<unsigned char>
        get_used_boundary_indicators () const;

        /**
         * Declare the parameters this class takes through input files.
         */
        static
        void
        declare_parameters (ParameterHandler &prm);

        /**
         * Read the parameters this class declares from the parameter
         * file.
         */
        virtual
        void
        parse_parameters (ParameterHandler &prm);

      private:
        /**
         * Extent of the box in x-, y-, and z-direction (in 3d).
         */
        Point<dim> extents;
    };
  }
}


#endif
