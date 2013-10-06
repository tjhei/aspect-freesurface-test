/*
  Copyright (C) 2011, 2012, 2013 by the authors of the ASPECT code.

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

#include <deal.II/base/utilities.h>
#include <deal.II/base/mpi.h>

#include <dlfcn.h>



// get the value of a particular parameter from the input
// file. return an empty string if not found
std::string
get_last_value_of_parameter(const std::string &parameter_filename,
                            const std::string &parameter_name)
{
  std::string return_value;

  std::ifstream x_file(parameter_filename.c_str());
  while (x_file)
    {
      // get one line and strip spaces at the front and back
      std::string line;
      std::getline(x_file, line);
      while ((line.size() > 0) && (line[0] == ' ' || line[0] == '\t'))
        line.erase(0, 1);
      while ((line.size() > 0)
             && (line[line.size() - 1] == ' ' || line[line.size() - 1] == '\t'))
        line.erase(line.size() - 1, std::string::npos);
      // now see whether the line starts with 'set' followed by multiple spaces
      // if not, try next line
      if (line.size() < 4)
        continue;

      if ((line[0] != 's') || (line[1] != 'e') || (line[2] != 't')
          || !(line[3] == ' ' || line[3] == '\t'))
        continue;

      // delete the "set " and then delete more spaces if present
      line.erase(0, 4);
      while ((line.size() > 0) && (line[0] == ' ' || line[0] == '\t'))
        line.erase(0, 1);
      // now see whether the next word is the word we look for
      if (line.find(parameter_name) != 0)
        continue;

      line.erase(0, parameter_name.size());
      while ((line.size() > 0) && (line[0] == ' ' || line[0] == '\t'))
        line.erase(0, 1);

      // we'd expect an equals size here
      if ((line.size() < 1) || (line[0] != '='))
        continue;

      // trim the equals sign at the beginning and possibly following spaces
      // as well as spaces at the end
      line.erase(0, 1);
      while ((line.size() > 0) && (line[0] == ' ' || line[0] == '\t'))
        line.erase(0, 1);
      while ((line.size() > 0) && (line[line.size()-1] == ' ' || line[line.size()-1] == '\t'))
        line.erase(line.size()-1, std::string::npos);

      // the rest should now be what we were looking for
      return_value = line;
    }

  return return_value;
}


// extract the dimension in which to run ASPECT from the
// parameter file. this is something that we need to do
// before processing the parameter file since we need to
// know whether to use the dim=2 or dim=3 instantiation
// of the main classes
unsigned int
get_dimension(const std::string &parameter_filename)
{
  const std::string dimension = get_last_value_of_parameter(parameter_filename, "Dimension");
  if (dimension.size() > 0)
    return dealii::Utilities::string_to_int (dimension);
  else
    return 2;
}



// retrieve a list of shared libraries from the parameter file and
// dlopen them so that we can load plugins declared in them
void possibly_load_shared_libs (const std::string &parameter_filename)
{
  using namespace dealii;

  const std::string shared_libs
    = get_last_value_of_parameter(parameter_filename,
                                  "Additional shared libraries");
  if (shared_libs.size() > 0)
    {
      const std::vector<std::string>
      shared_libs_list = Utilities::split_string_list (shared_libs);

      for (unsigned int i=0; i<shared_libs_list.size(); ++i)
        {
          if (Utilities::MPI::this_mpi_process (MPI_COMM_WORLD) == 0)
            std::cout << "Loading shared library <"
                      << shared_libs_list[i]
                      << ">" << std::endl;

          void *handle = dlopen (shared_libs_list[i].c_str(), RTLD_LAZY);
          AssertThrow (handle != NULL,
                       ExcMessage (std::string("Could not successfully load shared library <")
                                   + shared_libs_list[i] + ">. The operating system reports "
                                   + "that the error is this: <"
                                   + dlerror() + ">."));
        }

      std::cout << std::endl;
    }
}


int main (int argc, char *argv[])
{
  using namespace dealii;
  // use numbers::invalid_unsigned_int instead of 1 to use as many threads
  // as deemed useful by TBB
  unsigned int n_threads = 1;
  Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv, n_threads);

  try
    {
      deallog.depth_console(0);

      // see which parameter file to use
      std::string parameter_filename;
      if (argc >= 2)
        parameter_filename = argv[1];
      else
        parameter_filename = "box.prm";

      // verify that it can be opened
      {
        std::ifstream parameter_file(parameter_filename.c_str());
        if (!parameter_file)
          {
            const std::string message = (std::string("Input parameter file <")
                                         + parameter_filename + "> not found.");
            AssertThrow(false, ExcMessage (message));
          }
      }
      // now that we know that the file can, at least in principle, be read
      // try to determine the dimension we want to work in. the default
      // is 2, but if we find a line of the kind "set Dimension = ..."
      // then the last such line wins
      const unsigned int dim = get_dimension(parameter_filename);

      // do the same with lines potentially indicating shared libs to
      // be loaded. these shared libs could contain additional module
      // instantiations for geometries, etc, that would then be
      // available as part of the possible parameters of the input
      // file, so they need to be loaded before we even start processing
      // the parameter file
      possibly_load_shared_libs (parameter_filename);

      // now switch between the templates that code for 2d or 3d. it
      // would be nicer if we didn't have to duplicate code, but the
      // following needs to be known at compile time whereas the dimensionality
      // is only read at run-time
      ParameterHandler prm;

      std::ifstream parameter_file(parameter_filename.c_str());
      switch (dim)
        {
          case 2:
          {
            aspect::Simulator<2>::declare_parameters(prm);

            const bool success = prm.read_input(parameter_file);
            AssertThrow(success, ExcMessage ("Invalid input parameter file."));

            aspect::Simulator<2> flow_problem(MPI_COMM_WORLD, prm);
            flow_problem.run();

            break;
          }

          case 3:
          {
            aspect::Simulator<3>::declare_parameters(prm);

            const bool success = prm.read_input(parameter_file);
            AssertThrow(success, ExcMessage ("Invalid input parameter file."));

            aspect::Simulator<3> flow_problem(MPI_COMM_WORLD, prm);
            flow_problem.run();

            break;
          }

          default:
            AssertThrow((dim >= 2) && (dim <= 3),
                        ExcMessage ("ASPECT can only be run in 2d and 3d but a "
                                    "different space dimension is given in the parameter file."));
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
