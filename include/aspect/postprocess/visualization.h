/*
  Copyright (C) 2011, 2012 by the authors of the ASPECT code.

  This file is part of ASPECT.

  ASPECT is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  ASPECT is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with ASPECT; see the file doc/COPYING.  If not see
  <http://www.gnu.org/licenses/>.
*/
/*  $Id$  */


#ifndef __aspect__postprocess_visualization_h
#define __aspect__postprocess_visualization_h

#include <aspect/postprocess/interface.h>
#include <aspect/simulator_access.h>
#include <aspect/plugins.h>

#include <deal.II/base/thread_management.h>
#include <deal.II/numerics/data_postprocessor.h>
#include <deal.II/base/data_out_base.h>

namespace aspect
{
  namespace Postprocess
  {
    namespace VisualizationPostprocessors
    {
      /**
       * This class declares the public interface of visualization
       * postprocessors. Visualization postprocessors are used to compute
       * derived data, e.g. wave speeds, friction heating terms, etc, to be
       * put into graphical output files. They are plugins for the
       * aspect::Postprocess::Visualization class.
       *
       * Classes derived from this type must implement the functions that save
       * the state of the object and restore it (for checkpoint/restart
       * capabilities) as well as functions that declare and read parameters.
       * However, this class also already provides default implementations of
       * these functions that simply do nothing. This is appropriate for
       * objects that are stateless, as is commonly the case for visualization
       * postprocessors.
       *
       * Access to the data of the simulator is granted by the @p protected
       * member functions of the SimulatorAccess class, i.e., classes
       * implementing this interface will in general want to derive from both
       * this Interface class as well as from the SimulatorAccess class.
       *
       * <h3> How visualization plugins work </h3>
       *
       * There are two ways in which visualization plugins can work to get
       * data from a simulation into an output file:
       * <ul>
       * <li> Classes derived from this class can also derive from the deal.II
       * class DataPostprocessor or any of the classes like
       * DataPostprocessorScalar or DataPostprocessorVector. These classes can
       * be thought of as filters: DataOut will call a function in them for
       * every cell and this function will transform the values or gradients
       * of the solution and other information such as the location of
       * quadrature points into the desired quantity to output. A typical case
       * would be if the quantity $g(x)$ you want to output can be written as
       * a function $g(x) = G(u(x),\nabla u(x), x, ...)$ in a pointwise sense
       * where $u(x)$ is the value of the solution vector (i.e., the
       * velocities, pressure, temperature, etc) at an evaluation point. In
       * the context of this program an example would be to output the density
       * of the medium as a spatially variable function since this is a
       * quantity that for realistic media depends pointwise on the values of
       * the solution.
       *
       * Using this way of describing a visualization postprocessor will yield
       * a class  that would then have the following base classes: -
       * aspect::Postprocess::VisualizationPostprocessors::Interface -
       * aspect::SimulatorAccess - dealii::DataPostprocessor or any of the
       * other ones listed above
       *
       * <li> The second possibility is for a class to not derive from
       * dealii::DataPostprocessor but instead from the CellDataVectorCreator
       * class. In this case, a visualization postprocessor would generate and
       * return a vector that consists of one element per cell. The intent of
       * this option is to output quantities that are not pointwise functions
       * of the solution but instead can only be computed as integrals or
       * other functionals on a per-cell basis. A typical case would be error
       * estimators that do depend on the solution but not in a pointwise
       * sense; rather, they yield one value per cell of the mesh. See the
       * documentation of the CellDataVectorCreator class for more
       * information.
       * </ul>
       * @ingroup Postprocessing
       * @ingroup Visualization
       */
      template <int dim>
      class Interface
      {
        public:
          /**
           * Destructor. Does nothing but is virtual so that derived classes
           * destructors are also virtual.
           */
          virtual
          ~Interface ();

          /**
           * Declare the parameters this class takes through input files.
           * Derived classes should overload this function if they actually do
           * take parameters; this class declares a fall-back function that
           * does nothing, so that postprocessor classes that do not take any
           * parameters do not have to do anything at all.
           *
           * This function is static (and needs to be static in derived
           * classes) so that it can be called without creating actual objects
           * (because declaring parameters happens before we read the input
           * file and thus at a time when we don't even know yet which
           * postprocessor objects we need).
           */
          static
          void
          declare_parameters (ParameterHandler &prm);

          /**
           * Read the parameters this class declares from the parameter file.
           * The default implementation in this class does nothing, so that
           * derived classes that do not need any parameters do not need to
           * implement it.
           */
          virtual
          void
          parse_parameters (ParameterHandler &prm);


          /**
           * Save the state of this object to the argument given to this
           * function. This function is in support of checkpoint/restart
           * functionality.
           *
           * Derived classes can implement this function and should store
           * their state in a string that is deposited under a key in the map
           * through which the respective class can later find the status
           * again when the program is restarted. A legitimate key to store
           * data under is <code>typeid(*this).name()</code>. It is up to
           * derived classes to decide how they want to encode their state.
           *
           * The default implementation of this function does nothing, i.e.,
           * it represents a stateless object for which nothing needs to be
           * stored at checkpoint time and nothing needs to be restored at
           * restart time.
           *
           * @param[in,out] status_strings The object into which
           * implementations in derived classes can place their status under a
           * key that they can use to retrieve the data.
           */
          virtual
          void save (std::map<std::string, std::string> &status_strings) const;

          /**
           * Restore the state of the object by looking up a description of
           * the state in the passed argument under the same key under which
           * it was previously stored.
           *
           * The default implementation does nothing.
           *
           * @param[in] status_strings The object from which the status will
           * be restored by looking up the value for a key specific to this
           * derived class.
           */
          virtual
          void load (const std::map<std::string, std::string> &status_strings);
      };



      /**
       * As explained in the documentation of the Interface class, the second
       * kind of visualization plugin is one that wants to generate cellwise
       * data. Classes derived from this class need to implement a function
       * execute() that computes these cellwise values and return a pair of
       * values where the first one indicates the name of a variable and the
       * second one is a vector with one entry per cell. This class is the
       * interface that such plugins have to implement.
       *
       * @ingroup Postprocessing
       * @ingroup Visualization
       */
      template <int dim>
      class CellDataVectorCreator : public Interface<dim>
      {
        public:
          /**
           * The function classes have to implement that want to output
           * cellwise data. @return A pair of values with the following
           * meaning: - The first element provides the name by which this data
           * should be written to the output file. - The second element is a
           * pointer to a vector with one element per active cell on the
           * current processor. Elements corresponding to active cells that
           * are either artificial or ghost cells (in deal.II language, see
           * the deal.II glossary) will be ignored but must nevertheless exist
           * in the returned vector. While implementations of this function
           * must create this vector, ownership is taken over by the caller of
           * this function and the caller will take care of destroying the
           * vector pointed to.
           */
          virtual
          std::pair<std::string, Vector<float> *>
          execute () const = 0;
      };
    }


    /**
     * A postprocessor that generates graphical output in periodic intervals
     * or every time step. The time interval between generating graphical
     * output is obtained from the parameter file.
     *
     * While this class acts as a plugin, i.e. as a postprocessor that can be
     * registered with the postprocessing manager class
     * aspect::Postprocess::Manager, this class at the same time also acts as
     * a manager for plugins itself, namely for classes derived from the
     * VisualizationPostprocessors::Interface class that are used to output
     * different aspects of the solution, such as for example a computed
     * seismic wave speed from temperature, velocity and pressure.
     *
     * @ingroup Postprocessing
     */
    template <int dim>
    class Visualization : public Interface<dim>, public ::aspect::SimulatorAccess<dim>
    {
      public:
        /**
         * Constructor.
         */
        Visualization ();

        /**
         * Destructor. Makes sure that any background thread that may still be
         * running writing data to disk finishes before the current object is
         * fully destroyed.
         */
        ~Visualization ();

        /**
         * Generate graphical output from the current solution.
         */
        virtual
        std::pair<std::string,std::string>
        execute (TableHandler &statistics);

        /**
         * Initialize this class for a given simulator. In addition to calling
         * the respective function from the base class, this function also
         * initializes all the visualization postprocessor plugins.
         *
         * @param simulator A reference to the main simulator object to which
         * the postprocessor implemented in the derived class should be
         * applied.
         */
        virtual void initialize (const Simulator<dim> &simulator);


        /**
         * A function that is used to register visualization postprocessor
         * objects in such a way that the Manager can deal with all of them
         * without having to know them by name. This allows the files in which
         * individual postprocessors are implement to register these
         * postprocessors, rather than also having to modify the Manage class
         * by adding the new postprocessor class.
         *
         * @param name The name under which this visualization postprocessor
         * is to be called in parameter files.
         * @param description A text description of what this visualization
         * plugin does and that will be listed in the documentation of the
         * parameter file.
         * @param declare_parameters_function A pointer to a function that
         * declares the parameters for this postprocessor.
         * @param factory_function A pointer to a function that creates such a
         * postprocessor object and returns a pointer to it.
         */
        static
        void
        register_visualization_postprocessor (const std::string &name,
                                              const std::string &description,
                                              void (*declare_parameters_function) (ParameterHandler &),
                                              VisualizationPostprocessors::Interface<dim> *(*factory_function) ());

        /**
         * Declare the parameters this class takes through input files.
         */
        static
        void
        declare_parameters (ParameterHandler &prm);

        /**
         * Read the parameters this class declares from the parameter file.
         */
        virtual
        void
        parse_parameters (ParameterHandler &prm);

        /**
         * Save the state of this object.
         */
        virtual
        void save (std::map<std::string, std::string> &status_strings) const;

        /**
         * Restore the state of the object.
         */
        virtual
        void load (const std::map<std::string, std::string> &status_strings);

        /**
         * Serialize the contents of this class as far as they are not read
         * from input parameter files.
         */
        template <class Archive>
        void serialize (Archive &ar, const unsigned int version);

        /**
         * Exception.
         */
        DeclException1 (ExcPostprocessorNameNotFound,
                        std::string,
                        << "Could not find entry <"
                        << arg1
                        << "> among the names of registered postprocessors.");

      private:
        /**
         * Interval between the generation of graphical output. This parameter
         * is read from the input file and consequently is not part of the
         * state that needs to be saved and restored.
         *
         * For technical reasons, this value is stored as given in the input
         * file and upon use is either interpreted as seconds or years,
         * depending on how the global flag in the input parameter file is
         * set.
         */
        double output_interval;

        /**
         * A time (in years) after which the next time step should produce
         * graphical output again.
         */
        double next_output_time;

        /**
         * Consecutively counted number indicating the how-manyth time we will
         * create output the next time we get to it.
         */
        unsigned int output_file_number;

        /**
         * Graphical output format.
         */
        std::string output_format;

        /**
         * VTU file output supports grouping files from several CPUs into one
         * file using MPI I/O when writing on a parallel filesystem. 0 means
         * no grouping (and no parallel I/O). 1 will generate one big file
         * containing the whole solution.
         */
        unsigned int group_files;

        /**
         * Compute the next output time from the current one. In the simplest
         * case, this is simply the previous next output time plus the
         * interval, but in general we'd like to ensure that it is larger than
         * the current time to avoid falling behind with next_output_time and
         * having to catch up once the time step becomes larger.
         */
        void set_next_output_time (const double current_time);

        /**
         * Record that the mesh changed. This helps some output writers avoid
         * writing the same mesh multiple times.
         */
        void mesh_changed_signal ();

        /**
         * Whether the mesh changed since the last output.
         */
        bool mesh_changed;

        /**
         * The most recent name of the mesh file, used to avoid redundant mesh
         * output.
         */
        std::string last_mesh_file_name;

        /**
         * Handle to a thread that is used to write data in the background.
         * The background_writer() function runs on this background thread.
         */
        Threads::Thread<void> background_thread;

        /**
         * A function that writes the text in the second argument to a file
         * with the name given in the first argument. The function is run on a
         * separate thread to allow computations to continue even though
         * writing data is still continuing. The function takes over ownership
         * of these arguments and deletes them at the end of its work.
         */
        static
        void background_writer (const std::string *filename,
                                const std::string *file_contents);

        /**
         * A list of postprocessor objects that have been requested in the
         * parameter file.
         */
        std::list<std_cxx1x::shared_ptr<VisualizationPostprocessors::Interface<dim> > > postprocessors;

        /**
         * A list of pairs (time, pvtu_filename) that have so far been written
         * and that we will pass to DataOutInterface::write_pvd_record to
         * create a master file that can make the association between
         * simulation time and corresponding file name (this is done because
         * there is no way to store the simulation time inside the .pvtu or
         * .vtu files).
         */
        std::vector<std::pair<double,std::string> > times_and_pvtu_names;

        /**
         * A list of list of filenames, sorted by timestep, that correspond to
         * what has been created as output. This is used to create a master
         * .visit file for the entire simulation.
         */
        std::vector<std::vector<std::string> > output_file_names_by_timestep;

        /**
         * A set of data related to XDMF file sections describing the HDF5
         * heavy data files created. These contain things such as the
         * dimensions and names of data written at all steps during the
         * simulation.
         */
        std::vector<XDMFEntry>  xdmf_entries;
    };
  }


  /**
   * Given a class name, a name, and a description for the parameter file for
   * a postprocessor, register it with the aspect::Postprocess::Manager class.
   *
   * @ingroup Postprocessing
   */
#define ASPECT_REGISTER_VISUALIZATION_POSTPROCESSOR(classname,name,description) \
  template class classname<2>; \
  template class classname<3>; \
  namespace ASPECT_REGISTER_VISUALIZATION_POSTPROCESSOR_ ## classname \
  { \
    aspect::internal::Plugins::RegisterHelper<VisualizationPostprocessors::Interface<2>,classname<2> > \
    dummy_ ## classname ## _2d (&aspect::Postprocess::Visualization<2>::register_visualization_postprocessor, \
                                name, description); \
    aspect::internal::Plugins::RegisterHelper<VisualizationPostprocessors::Interface<3>,classname<3> > \
    dummy_ ## classname ## _3d (&aspect::Postprocess::Visualization<3>::register_visualization_postprocessor, \
                                name, description); \
  }
}


#endif
