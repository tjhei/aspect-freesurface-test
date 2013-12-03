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


#include <aspect/simulator.h>
#include <aspect/global.h>

#include <deal.II/base/parameter_handler.h>

#include <dirent.h>


namespace aspect
{
  template <int dim>
  Simulator<dim>::Parameters::Parameters (ParameterHandler &prm)
  {
    parse_parameters (prm);
  }


  template <int dim>
  void
  Simulator<dim>::Parameters::
  declare_parameters (ParameterHandler &prm)
  {
    prm.declare_entry ("Dimension", "2",
                       Patterns::Integer (2,4),
                       "The number of space dimensions you want to run this program in. "
                       "ASPECT can run in 2 and 3 space dimensions.");
    prm.declare_entry ("Additional shared libraries", "",
                       Patterns::List (Patterns::FileName()),
                       "A list of names of additional shared libraries that should be loaded "
                       "upon starting up the program. The names of these files can contain absolute "
                       "or relative paths (relative to the directory in which you call ASPECT). "
                       "In fact, file names that are do not contain any directory "
                       "information (i.e., only the name of a file such as <myplugin.so> "
                       "will not be found if they are not located in one of the directories "
                       "listed in the LD_LIBRARY_PATH environment variable. In order "
                       "to load a library in the current directory, use <./myplugin.so> "
                       "instead."
                       "\n\n"
                       "The typical use of this parameter is so that you can implement "
                       "additional plugins in your own directories, rather than in the ASPECT "
                       "source directories. You can then simply compile these plugins into a "
                       "shared library without having to re-compile all of ASPECT. See the "
                       "section of the manual discussing writing extensions for more "
                       "information on how to compile additional files into a shared "
                       "library.");

    prm.declare_entry ("Resume computation", "false",
                       Patterns::Bool (),
                       "A flag indicating whether the computation should be resumed from "
                       "a previously saved state (if true) or start from scratch (if false).");

    prm.declare_entry ("Max nonlinear iterations", "10",
                       Patterns::Integer (0),
                       "The maximal number of nonlinear iterations to be performed.");

    prm.declare_entry ("Start time", "0",
                       Patterns::Double (),
                       "The start time of the simulation. Units: years if the "
                       "'Use years in output instead of seconds' parameter is set; "
                       "seconds otherwise.");

    prm.declare_entry ("Timing output frequency", "100",
                       Patterns::Integer(0),
                       "How frequently in timesteps to output timing information. This is "
                       "generally adjusted only for debugging and timing purposes.");

    prm.declare_entry ("Use years in output instead of seconds", "true",
                       Patterns::Bool (),
                       "When computing results for mantle convection simulations, "
                       "it is often difficult to judge the order of magnitude of results "
                       "when they are stated in MKS units involving seconds. Rather, "
                       "some kinds of results such as velocities are often stated in "
                       "terms of meters per year (or, sometimes, centimeters per year). "
                       "On the other hand, for non-dimensional computations, one wants "
                       "results in their natural unit system as used inside the code. "
                       "If this flag is set to 'true' conversion to years happens; if "
                       "it is 'false', no such conversion happens.");

    prm.declare_entry ("CFL number", "1.0",
                       Patterns::Double (0),
                       "In computations, the time step $k$ is chosen according to "
                       "$k = c \\min_K \\frac {h_K} {\\|u\\|_{\\infty,K} p_T}$ where $h_K$ is the "
                       "diameter of cell $K$, and the denominator is the maximal magnitude "
                       "of the velocity on cell $K$ times the polynomial degree $p_T$ of the "
                       "temperature discretization. The dimensionless constant $c$ is called the "
                       "CFL number in this program. For time discretizations that have explicit "
                       "components, $c$ must be less than a constant that depends on the "
                       "details of the time discretization and that is no larger than one. "
                       "On the other hand, for implicit discretizations such as the one chosen "
                       "here, one can choose the time step as large as one wants (in particular, "
                       "one can choose $c>1$) though a CFL number significantly larger than "
                       "one will yield rather diffusive solutions. Units: None.");

    prm.declare_entry ("Use conduction timestep", "false",
                       Patterns::Bool (),
                       "Mantle convection simulations are often focused on convection "
                       "dominated systems. However, these codes can also be used to "
                       "investigate systems where heat conduction plays a dominant role. "
                       "This parameter indicates whether the simulator should also use "
                       "heat conduction in determining the length of each time step.");

    prm.declare_entry ("Nonlinear solver scheme", "IMPES",
                       Patterns::Selection ("IMPES|iterated IMPES|iterated Stokes|Stokes only"),
                       "The kind of scheme used to resolve the nonlinearity in the system. "
                       "'IMPES' is the classical IMplicit Pressure Explicit Saturation scheme "
                       "in which ones solves the temperatures and Stokes equations exactly "
                       "once per time step, one after the other. The 'iterated IMPES' scheme "
                       "iterates this decoupled approach by alternating the solution of the "
                       "temperature and Stokes systems. The 'iterated Stokes' scheme solves "
                       "the temperature equation once at the beginning of each time step "
                       "and then iterates out the solution of the Stokes equation. The 'Stokes only' "
                       "scheme only solves the Stokes system and ignores compositions and the "
                       "temperature equation (careful, the material model must not depend on "
                       "the temperature; mostly useful for Stokes benchmarks).");

    prm.declare_entry ("Nonlinear solver tolerance", "1e-5",
                       Patterns::Double(0,1),
                       "A relative tolerance up to which the nonlinear solver "
                       "will iterate. This parameter is only relevant if "
                       "Nonlinear solver scheme is set to 'iterated Stokes' or "
                       "'iterated IMPES'.");

    prm.declare_entry ("Pressure normalization", "surface",
                       Patterns::Selection ("surface|volume|no"),
                       "If and how to normalize the pressure after the solution step. "
                       "This is necessary because depending on boundary conditions, "
                       "in many cases the pressure is only determined by the model "
                       "up to a constant. On the other hand, we often would like to "
                       "have a well-determined pressure, for example for "
                       "table lookups of material properties in models "
                       "or for comparing solutions. If the given value is `surface', then "
                       "normalization at the end of each time steps adds a constant value "
                       "to the pressure in such a way that the average pressure at the surface "
                       "of the domain is zero; the surface of the domain is determined by asking "
                       "the geometry model whether a particular face of the geometry has a zero "
                       "or small `depth'. If the value of this parameter is `volume' then the "
                       "pressure is normalized so that the domain average is zero. If `no' is "
                       "given, the no pressure normalization is performed.");

    prm.declare_entry ("Surface pressure", "0",
                       Patterns::Double(),
                       "The mathematical equations that describe thermal convection "
                       "only determine the pressure up to an arbitrary constant. On "
                       "the other hand, for comparison and for looking up material "
                       "parameters it is important that the pressure be normalized "
                       "somehow. We do this by enforcing a particular average pressure "
                       "value at the surface of the domain, where the geometry model "
                       "determines where the surface is. This parameter describes what "
                       "this average surface pressure value is supposed to be. By "
                       "default, it is set to zero, but one may want to choose a "
                       "different value for example for simulating only the volume "
                       "of the mantle below the lithosphere, in which case the surface "
                       "pressure should be the lithostatic pressure at the bottom "
                       "of the lithosphere."
                       "\n"
                       "For more information, see the section in the manual that discusses "
                       "the general mathematical model.");

    prm.declare_entry ("Adiabatic surface temperature", "0",
                       Patterns::Double(),
                       "In order to make the problem in the first time step easier to "
                       "solve, we need a reasonable guess for the temperature and pressure. "
                       "To obtain it, we use an adiabatic pressure and temperature field. "
                       "This parameter describes what the `adiabatic' temperature would "
                       "be at the surface of the domain (i.e. at depth zero). Note "
                       "that this value need not coincide with the boundary condition "
                       "posed at this point. Rather, the boundary condition may differ "
                       "significantly from the adiabatic value, and then typically "
                       "induce a thermal boundary layer."
                       "\n"
                       "For more information, see the section in the manual that discusses "
                       "the general mathematical model.");

    prm.declare_entry ("Output directory", "output",
                       Patterns::DirectoryName(),
                       "The name of the directory into which all output files should be "
                       "placed. This may be an absolute or a relative path.");

    prm.declare_entry ("Linear solver tolerance", "1e-7",
                       Patterns::Double(0,1),
                       "A relative tolerance up to which the linear Stokes systems in each "
                       "time or nonlinear step should be solved. The absolute tolerance will "
                       "then be the norm of the right hand side of the equation "
                       "times this tolerance. A given tolerance value of 1 would "
                       "mean that a zero solution vector is an acceptable solution "
                       "since in that case the norm of the residual of the linear "
                       "system equals the norm of the right hand side. A given "
                       "tolerance of 0 would mean that the linear system has to be "
                       "solved exactly, since this is the only way to obtain "
                       "a zero residual."
                       "\n\n"
                       "In practice, you should choose the value of this parameter "
                       "to be so that if you make it smaller the results of your "
                       "simulation do not change any more (qualitatively) whereas "
                       "if you make it larger, they do. For most cases, the default "
                       "value should be sufficient. However, for cases where the "
                       "static pressure is much larger than the dynamic one, it may "
                       "be necessary to choose a smaller value.");
    prm.declare_entry ("Number of cheap Stokes solver steps", "30",
                       Patterns::Integer(0),
                       "As explained in the ASPECT paper (Kronbichler, Heister, and Bangerth, "
                       "GJI 2012) we first try to solve the Stokes system in every time "
                       "step using a GMRES iteration with a poor but cheap "
                       "preconditioner. By default, we try whether we can converge the GMRES "
                       "solver in 30 such iterations before deciding that we need a better "
                       "preconditioner. This is sufficient for simple problems with constant "
                       "viscosity and we never need the second phase with the more expensive "
                       "preconditioner. On the other hand, for more complex problems, and in "
                       "particular for problems with strongly varying viscosity, the 30 "
                       "cheap iterations don't actually do very much good and one might skip "
                       "this part right away. In that case, this parameter can be set to "
                       "zero, i.e., we immediately start with the better but more expensive "
                       "preconditioner.");

    prm.declare_entry ("Temperature solver tolerance", "1e-12",
                       Patterns::Double(0,1),
                       "The relative tolerance up to which the linear system for "
                       "the temperature system gets solved. See 'linear solver "
                       "tolerance' for more details.");

    prm.declare_entry ("Composition solver tolerance", "1e-12",
                       Patterns::Double(0,1),
                       "The relative tolerance up to which the linear system for "
                       "the composition system gets solved. See 'linear solver "
                       "tolerance' for more details.");

    prm.enter_subsection ("Model settings");
    {
      prm.declare_entry ("Include shear heating", "true",
                         Patterns::Bool (),
                         "Whether to include shear heating into the model or not. From a "
                         "physical viewpoint, shear heating should always be used but may "
                         "be undesirable when comparing results with known benchmarks that "
                         "do not include this term in the temperature equation.");
      prm.declare_entry ("Include adiabatic heating", "false",
                         Patterns::Bool (),
                         "Whether to include adiabatic heating into the model or not. From a "
                         "physical viewpoint, adiabatic heating should always be used but may "
                         "be undesirable when comparing results with known benchmarks that "
                         "do not include this term in the temperature equation.");
      prm.declare_entry ("Include latent heat", "false",
                         Patterns::Bool (),
                         "Whether to include the generation of latent heat at phase transitions "
                         "into the model or not. From a physical viewpoint, latent heat should "
                         "always be used but may be undesirable when comparing results with known "
                         "benchmarks that do not include this term in the temperature equation "
                         "or when dealing with a model without phase transitions.");
      prm.declare_entry ("Radiogenic heating rate", "0e0",
                         Patterns::Double (),
                         "H0");
      prm.declare_entry ("Fixed temperature boundary indicators", "",
                         Patterns::List (Patterns::Integer(0)),
                         "A comma separated list of integers denoting those boundaries "
                         "on which the temperature is fixed and described by the "
                         "boundary temperature object selected in its own section "
                         "of this input file. All boundary indicators used by the geometry "
                         "but not explicitly listed here will end up with no-flux "
                         "(insulating) boundary conditions."
                         "\n\n"
                         "This parameter only describes which boundaries have a fixed "
                         "temperature, but not what temperature should hold on these "
                         "boundaries. The latter piece of information needs to be "
                         "implemented in a plugin in the BoundaryTemperature "
                         "group, unless an existing implementation in this group "
                         "already provides what you want.");
      prm.declare_entry ("Fixed composition boundary indicators", "",
                         Patterns::List (Patterns::Integer(0)),
                         "A comma separated list of integers denoting those boundaries "
                         "on which the composition is fixed and described by the "
                         "boundary composition object selected in its own section "
                         "of this input file. All boundary indicators used by the geometry "
                         "but not explicitly listed here will end up with no-flux "
                         "(insulating) boundary conditions."
                         "\n\n"
                         "This parameter only describes which boundaries have a fixed "
                         "composition, but not what composition should hold on these "
                         "boundaries. The latter piece of information needs to be "
                         "implemented in a plugin in the BoundaryComposition "
                         "group, unless an existing implementation in this group "
                         "already provides what you want.");
      prm.declare_entry ("Zero velocity boundary indicators", "",
                         Patterns::List (Patterns::Integer(0, std::numeric_limits<types::boundary_id>::max())),
                         "A comma separated list of integers denoting those boundaries "
                         "on which the velocity is zero.");
      prm.declare_entry ("Tangential velocity boundary indicators", "",
                         Patterns::List (Patterns::Integer(0, std::numeric_limits<types::boundary_id>::max())),
                         "A comma separated list of integers denoting those boundaries "
                         "on which the velocity is tangential and unrestrained, i.e., free-slip where "
                         "no external forces act to prescribe a particular tangential "
                         "velocity (although there is a force that requires the flow to "
                         "be tangential).");
      prm.declare_entry ("Prescribed velocity boundary indicators", "",
                         Patterns::Map (Patterns::Anything(),
                                        Patterns::Selection(VelocityBoundaryConditions::get_names<dim>())),
                         "A comma separated list denoting those boundaries "
                         "on which the velocity is tangential but prescribed, i.e., where "
                         "external forces act to prescribe a particular velocity. This is "
                         "often used to prescribe a velocity that equals that of "
                         "overlying plates."
                         "\n\n"
                         "The format of valid entries for this parameter is that of a map "
                         "given as ``key1 [selector]: value1, key2 [selector]: value2, key3: value3, ...'' where "
                         "each key must be a valid boundary indicator (which is an integer) "
                         "and each value must be one of the currently implemented boundary "
                         "velocity models. selector is an optional string given as a subset "
                         "of the letters 'xyz' that allows you to apply the boundary conditions "
                         "only to the components listed. As an example, '1 y: function' applies "
                         "the type 'function' to the y component on boundary 1. Without a selector "
                         "it will effect all components of the velocity."
                         "\n\n"
                         "Note that the no-slip boundary condition is "
                         "a special case of the current one where the prescribed velocity "
                         "happens to be zero. It can thus be implemented by indicating that "
                         "a particular boundary is part of the ones selected "
                         "using the current parameter and using ``zero velocity'' as "
                         "the boundary values. Alternatively, you can simply list the "
                         "part of the boundary on which the velocity is to be zero with "
                         "the parameter ``Zero velocity boundary indicator'' in the "
                         "current parameter section.");
    }
    prm.leave_subsection ();

    prm.enter_subsection ("Mesh refinement");
    {
      prm.declare_entry ("Initial global refinement", "2",
                         Patterns::Integer (0),
                         "The number of global refinement steps performed on "
                         "the initial coarse mesh, before the problem is first "
                         "solved there.");
      prm.declare_entry ("Initial adaptive refinement", "2",
                         Patterns::Integer (0),
                         "The number of adaptive refinement steps performed after "
                         "initial global refinement but while still within the first "
                         "time step.");
      prm.declare_entry ("Time steps between mesh refinement", "10",
                         Patterns::Integer (0),
                         "The number of time steps after which the mesh is to be "
                         "adapted again based on computed error indicators. If 0 "
                         "then the mesh will never be changed.");
      prm.declare_entry ("Refinement fraction", "0.3",
                         Patterns::Double(0,1),
                         "The fraction of cells with the largest error that "
                         "should be flagged for refinement.");
      prm.declare_entry ("Coarsening fraction", "0.05",
                         Patterns::Double(0,1),
                         "The fraction of cells with the smallest error that "
                         "should be flagged for coarsening.");
      prm.declare_entry ("Minimum refinement level", "0",
                         Patterns::Integer (0),
                         "The minimum refinement level each cell should have, "
                         "and that can not be exceeded by coarsening. "
                         "Should be higher than Initial global refinement.");
      prm.declare_entry ("Additional refinement times", "",
                         Patterns::List (Patterns::Double(0)),
                         "A list of times so that if the end time of a time step "
                         "is beyond this time, an additional round of mesh refinement "
                         "is triggered. This is mostly useful to make sure we "
                         "can get through the initial transient phase of a simulation "
                         "on a relatively coarse mesh, and then refine again when we "
                         "are in a time range that we are interested in and where "
                         "we would like to use a finer mesh. Units: each element of the "
                         "list has units years if the "
                         "'Use years in output instead of seconds' parameter is set; "
                         "seconds otherwise.");
      prm.declare_entry ("Run postprocessors on initial refinement", "false",
                         Patterns::Bool (),
                         "Whether or not the postproccessors should be run at the end "
                         "of each of ths initial adaptive refinement cycles at the "
                         "of the simulation start.");
    }
    prm.leave_subsection();

    prm.enter_subsection ("Checkpointing");
    {
      prm.declare_entry ("Time between checkpoint", "0",
                         Patterns::Integer (0),
                         "The wall time between performing checkpoints. "
                         "If 0, will use the checkpoint step frequency instead. "
                         "Units: Seconds.");
      prm.declare_entry ("Steps between checkpoint", "0",
                         Patterns::Integer (0),
                         "The number of timesteps between performing checkpoints. "
                         "If 0 and time between checkpoint is not specified, "
                         "checkpointing will not be performed. "
                         "Units: None.");
    }
    prm.leave_subsection ();

    prm.enter_subsection ("Discretization");
    {
      prm.declare_entry ("Stokes velocity polynomial degree", "2",
                         Patterns::Integer (1),
                         "The polynomial degree to use for the velocity variables "
                         "in the Stokes system. The polynomial degree for the pressure "
                         "variable will then be one less in order to make the velocity/pressure "
                         "pair conform with the usual LBB (Babuska-Brezzi) condition. In "
                         "other words, we are using a Taylor-Hood element for the Stoeks "
                         "equations and this parameter indicates the polynomial degree of it. "
                         "Units: None.");
      prm.declare_entry ("Temperature polynomial degree", "2",
                         Patterns::Integer (1),
                         "The polynomial degree to use for the temperature variable. "
                         "Units: None.");
      prm.declare_entry ("Composition polynomial degree", "2",
                         Patterns::Integer (1),
                         "The polynomial degree to use for the composition variable(s). "
                         "Units: None.");
      prm.declare_entry ("Use locally conservative discretization", "false",
                         Patterns::Bool (),
                         "Whether to use a Stokes discretization that is locally "
                         "conservative at the expense of a larger number of degrees "
                         "of freedom (true), or to go with a cheaper discretization "
                         "that does not locally conserve mass, although it is "
                         "globally conservative (false).\n\n"
                         "When using a locally "
                         "conservative discretization, the finite element space for "
                         "the pressure is discontinuous between cells and is the "
                         "polynomial space $P_ {-q}$ of polynomials of degree $q$ in "
                         "each variable separately. Here, $q$ is one less than the value "
                         "given in the parameter ``Stokes velocity polynomial degree''. "
                         "As a consequence of choosing this "
                         "element, it can be shown if the medium is considered incompressible "
                         "that the computed discrete velocity "
                         "field $\\mathbf u_h$ satisfies the property $\\int_ {\\partial K} \\mathbf u_h "
                         "\\cdot \\mathbf n = 0$ for every cell $K$, i.e., for each cell inflow and "
                         "outflow exactly balance each other as one would expect for an "
                         "incompressible medium. In other words, the velocity field is locally "
                         "conservative.\n\n"
                         "On the other hand, if this parameter is "
                         "set to ``false'', then the finite element space is chosen as $Q_q$. "
                         "This choice does not yield the local conservation property but "
                         "has the advantage of requiring fewer degrees of freedom. Furthermore, "
                         "the error is generally smaller with this choice.\n\n"
                         "For an in-depth discussion of these issues and a quantitative evaluation "
                         "of the different choices, see \\cite {KHB12} .");

      prm.enter_subsection ("Stabilization parameters");
      {
        prm.declare_entry ("alpha", "2",
                           Patterns::Integer (1, 2),
                           "The exponent $\\alpha$ in the entropy viscosity stabilization. Valid "
                           "options are 1 or 2. The recommended setting is 2. (This parameter does "
                           "not correspond to any variable in the 2012 GJI paper by Kronbichler, "
                           "Heister and Bangerth that describes ASPECT. Rather, the paper always uses "
                           "2 as the exponent in the definition of the entropy, following eq. (15).)."
                           "Units: None.");
        prm.declare_entry ("cR", "0.33",
                           Patterns::Double (0),
                           "The $c_R$ factor in the entropy viscosity "
                           "stabilization. (For historical reasons, the name used here is different "
                           "from the one used in the 2012 GJI paper by Kronbichler, "
                           "Heister and Bangerth that describes ASPECT. This parameter corresponds "
                           "to the factor $\\alpha_E$ in the formulas following equation (15) of "
                           "the paper. After further experiments, we have also chosen to use a "
                           "different value than described there.) Units: None.");
        prm.declare_entry ("beta", "0.078",
                           Patterns::Double (0),
                           "The $\\beta$ factor in the artificial viscosity "
                           "stabilization. An appropriate value for 2d is 0.078 "
                           "and 0.117 for 3d. (For historical reasons, the name used here is different "
                           "from the one used in the 2012 GJI paper by Kronbichler, "
                           "Heister and Bangerth that describes ASPECT. This parameter corresponds "
                           "to the factor $\\alpha_\\text {max}$ in the formulas following equation (15) of "
                           "the paper. After further experiments, we have also chosen to use a "
                           "different value than described there: It can be chosen as stated there for "
                           "uniformly refined meshes, but it needs to be chosen larger if the mesh has "
                           "cells that are not squares or cubes.) Units: None.");
      }
      prm.leave_subsection ();
    }
    prm.leave_subsection ();

    prm.enter_subsection ("Compositional fields");
    {
      prm.declare_entry ("Number of fields", "0",
                         Patterns::Integer (0),
                         "The number of fields that will be advected along with the flow field, excluding "
                         "velocity, pressure and temperature.");
      prm.declare_entry ("List of normalized fields", "",
                         Patterns::List (Patterns::Integer(0)),
                         "A list of integers smaller than or equal to the number of "
                         "compositional fields. All compositional fields in this "
                         "list will be normalized before the first timestep. "
                         "The normalization is implemented in the following way: "
                         "First, the sum of the fields to be normalized is calculated "
                         "at every point and the global maximum is determined. "
                         "Second, the compositional fields to be normalized are "
                         "divided by this maximum.");
    }
    prm.leave_subsection ();
  }



  template <int dim>
  void
  Simulator<dim>::Parameters::
  parse_parameters (ParameterHandler &prm)
  {
    // first, make sure that the ParameterHandler parser agrees
    // with the code in main() about the meaning of the "Dimension"
    // parameter
    AssertThrow (prm.get_integer("Dimension") == dim,
                 ExcInternalError());

    resume_computation      = prm.get_bool ("Resume computation");
    CFL_number              = prm.get_double ("CFL number");
    use_conduction_timestep = prm.get_bool ("Use conduction timestep");
    convert_to_years        = prm.get_bool ("Use years in output instead of seconds");
    timing_output_frequency = prm.get_integer ("Timing output frequency");

    if (prm.get ("Nonlinear solver scheme") == "IMPES")
      nonlinear_solver = NonlinearSolver::IMPES;
    else if (prm.get ("Nonlinear solver scheme") == "iterated IMPES")
      nonlinear_solver = NonlinearSolver::iterated_IMPES;
    else if (prm.get ("Nonlinear solver scheme") == "iterated Stokes")
      nonlinear_solver = NonlinearSolver::iterated_Stokes;
    else if (prm.get ("Nonlinear solver scheme") == "Stokes only")
      nonlinear_solver = NonlinearSolver::Stokes_only;
    else
      AssertThrow (false, ExcNotImplemented());

    nonlinear_tolerance = prm.get_double("Nonlinear solver tolerance");

    max_nonlinear_iterations = prm.get_integer ("Max nonlinear iterations");
    start_time              = prm.get_double ("Start time");
    if (convert_to_years == true)
      start_time *= year_in_seconds;

    output_directory        = prm.get ("Output directory");
    if (output_directory.size() == 0)
      output_directory = "./";
    else if (output_directory[output_directory.size()-1] != '/')
      output_directory += "/";

    // verify that the output directory actually exists. trying to
    // write to a non-existing output directory will eventually
    // produce an error but one not easily understood. since
    // this is no error where a nicely formatted error message
    // with a backtrace is likely very useful, just print an
    // error and exit
    if (opendir(output_directory.c_str()) == NULL)
      {
        std::cerr << "\n"
                  << "-----------------------------------------------------------------------------\n"
                  << "The output directory <" << output_directory
                  << "> provided in the input file appears not to exist!\n"
                  << "-----------------------------------------------------------------------------\n"
                  << std::endl;
        std::exit (1);
      }

    surface_pressure              = prm.get_double ("Surface pressure");
    adiabatic_surface_temperature = prm.get_double ("Adiabatic surface temperature");
    pressure_normalization        = prm.get("Pressure normalization");

    linear_stokes_solver_tolerance= prm.get_double ("Linear solver tolerance");
    n_cheap_stokes_solver_steps   = prm.get_integer ("Number of cheap Stokes solver steps");
    temperature_solver_tolerance  = prm.get_double ("Temperature solver tolerance");
    composition_solver_tolerance  = prm.get_double ("Composition solver tolerance");

    prm.enter_subsection ("Mesh refinement");
    {
      initial_global_refinement   = prm.get_integer ("Initial global refinement");
      initial_adaptive_refinement = prm.get_integer ("Initial adaptive refinement");

      adaptive_refinement_interval= prm.get_integer ("Time steps between mesh refinement");
      refinement_fraction         = prm.get_double ("Refinement fraction");
      coarsening_fraction         = prm.get_double ("Coarsening fraction");
      min_grid_level              = prm.get_integer ("Minimum refinement level");

      AssertThrow(refinement_fraction >= 0 && coarsening_fraction >= 0,
                  ExcMessage("Refinement/coarsening fractions must be positive."));
      AssertThrow(refinement_fraction+coarsening_fraction <= 1,
                  ExcMessage("Refinement and coarsening fractions must be <= 1."));
      AssertThrow(min_grid_level <= initial_global_refinement,
                  ExcMessage("Minimum refinement level must not be larger than "
                		  "Initial global refinement."));

      // extract the list of times at which additional refinement is requested
      // then sort it and convert it to seconds
      additional_refinement_times
        = Utilities::string_to_double
          (Utilities::split_string_list(prm.get ("Additional refinement times")));
      std::sort (additional_refinement_times.begin(),
                 additional_refinement_times.end());
      if (convert_to_years == true)
        for (unsigned int i=0; i<additional_refinement_times.size(); ++i)
          additional_refinement_times[i] *= year_in_seconds;

      run_postprocessors_on_initial_refinement = prm.get_bool("Run postprocessors on initial refinement");
    }
    prm.leave_subsection ();

    prm.enter_subsection ("Model settings");
    {
      include_shear_heating = prm.get_bool ("Include shear heating");
      include_adiabatic_heating = prm.get_bool ("Include adiabatic heating");
      include_latent_heat = prm.get_bool ("Include latent heat");
      radiogenic_heating_rate = prm.get_double ("Radiogenic heating rate");

      const std::vector<int> x_fixed_temperature_boundary_indicators
        = Utilities::string_to_int
          (Utilities::split_string_list
           (prm.get ("Fixed temperature boundary indicators")));
      fixed_temperature_boundary_indicators
        = std::set<types::boundary_id> (x_fixed_temperature_boundary_indicators.begin(),
                                        x_fixed_temperature_boundary_indicators.end());

      const std::vector<int> x_fixed_composition_boundary_indicators
        = Utilities::string_to_int
          (Utilities::split_string_list
           (prm.get ("Fixed composition boundary indicators")));
      fixed_composition_boundary_indicators
        = std::set<types::boundary_id> (x_fixed_composition_boundary_indicators.begin(),
                                          x_fixed_composition_boundary_indicators.end());

      const std::vector<int> x_zero_velocity_boundary_indicators
        = Utilities::string_to_int
          (Utilities::split_string_list
           (prm.get ("Zero velocity boundary indicators")));
      zero_velocity_boundary_indicators
        = std::set<types::boundary_id> (x_zero_velocity_boundary_indicators.begin(),
                                        x_zero_velocity_boundary_indicators.end());

      const std::vector<int> x_tangential_velocity_boundary_indicators
        = Utilities::string_to_int
          (Utilities::split_string_list
           (prm.get ("Tangential velocity boundary indicators")));
      tangential_velocity_boundary_indicators
        = std::set<types::boundary_id> (x_tangential_velocity_boundary_indicators.begin(),
                                        x_tangential_velocity_boundary_indicators.end());


      const std::vector<std::string> x_prescribed_velocity_boundary_indicators
        = Utilities::split_string_list
          (prm.get ("Prescribed velocity boundary indicators"));
      for (std::vector<std::string>::const_iterator p = x_prescribed_velocity_boundary_indicators.begin();
           p != x_prescribed_velocity_boundary_indicators.end(); ++p)
        {
          // each entry has the format (white space is optional):
          // <id> [x][y][z] : <value (might have spaces)>
          std::string comp = "";
          std::string value = "";

          std::stringstream ss(*p);
          int b_id;
          ss >> b_id; // need to read as int, not char
          types::boundary_id boundary_id = b_id;

          char c;
          while (ss.peek()==' ') ss.get(c); // eat spaces

          if (ss.peek()!=':')
            {
              std::getline(ss,comp,':');
              while (comp.length()>0 && *(--comp.end())==' ')
                comp.erase(comp.length()-1); // remove whitespace at the end
            }
          else
            ss.get(c); // read the ':'

          while (ss.peek()==' ') ss.get(c); // eat spaces

          std::getline(ss,value); // read until the end of the string

          AssertThrow (prescribed_velocity_boundary_indicators.find(boundary_id)
                       == prescribed_velocity_boundary_indicators.end(),
                       ExcMessage ("Boundary indicator <" + Utilities::int_to_string(boundary_id) +
                                   "> appears more than once in the list of indicators "
                                   "for nonzero velocity boundaries."));
          prescribed_velocity_boundary_indicators[boundary_id] =
              std::pair<std::string,std::string>(comp,value);
        }
    }
    prm.leave_subsection ();

    prm.enter_subsection ("Checkpointing");
    {
      checkpoint_time_secs = prm.get_integer ("Time between checkpoint");
      checkpoint_steps     = prm.get_integer ("Steps between checkpoint");
    }
    prm.leave_subsection ();

    prm.enter_subsection ("Discretization");
    {
      stokes_velocity_degree = prm.get_integer ("Stokes velocity polynomial degree");
      temperature_degree     = prm.get_integer ("Temperature polynomial degree");
      composition_degree     = prm.get_integer ("Composition polynomial degree");
      use_locally_conservative_discretization
        = prm.get_bool ("Use locally conservative discretization");

      prm.enter_subsection ("Stabilization parameters");
      {
        stabilization_alpha = prm.get_integer ("alpha");
        stabilization_c_R   = prm.get_double ("cR");
        stabilization_beta  = prm.get_double ("beta");
      }
      prm.leave_subsection ();
    }
    prm.leave_subsection ();

    prm.enter_subsection ("Compositional fields");
    {
      n_compositional_fields = prm.get_integer ("Number of fields");
      const std::vector<int> n_normalized_fields = Utilities::string_to_int
                                                   (Utilities::split_string_list(prm.get ("List of normalized fields")));
      normalized_fields = std::vector<unsigned int> (n_normalized_fields.begin(),
                                                     n_normalized_fields.end());

      AssertThrow (normalized_fields.size() <= n_compositional_fields,
                   ExcMessage("Invalid input parameter file: Too many entries in List of normalized fields"));
    }
    prm.leave_subsection ();
  }



  template <int dim>
  void Simulator<dim>::declare_parameters (ParameterHandler &prm)
  {
    Parameters::declare_parameters (prm);
    Postprocess::Manager<dim>::declare_parameters (prm);
    MeshRefinement::Manager<dim>::declare_parameters (prm);
    TerminationCriteria::Manager<dim>::declare_parameters (prm);
    MaterialModel::declare_parameters<dim> (prm);
    GeometryModel::declare_parameters <dim>(prm);
    GravityModel::declare_parameters<dim> (prm);
    InitialConditions::declare_parameters<dim> (prm);
    CompositionalInitialConditions::declare_parameters<dim> (prm);
    BoundaryTemperature::declare_parameters<dim> (prm);
    BoundaryComposition::declare_parameters<dim> (prm);
    VelocityBoundaryConditions::declare_parameters<dim> (prm);
  }
}


// explicit instantiation of the functions we implement in this file
namespace aspect
{
#define INSTANTIATE(dim) \
  template Simulator<dim>::Parameters::Parameters (ParameterHandler &prm); \
  template void Simulator<dim>::Parameters::declare_parameters (ParameterHandler &prm); \
  template void Simulator<dim>::Parameters::parse_parameters(ParameterHandler &prm); \
  template void Simulator<dim>::declare_parameters (ParameterHandler &prm);

  ASPECT_INSTANTIATE(INSTANTIATE)
}
