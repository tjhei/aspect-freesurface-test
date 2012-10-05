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

#ifndef __aspect__particle_world_h
#define __aspect__particle_world_h

#include <deal.II/numerics/fe_field_function.h>
#include <aspect/particle/particle.h>
#include <aspect/particle/integrator.h>
#include <aspect/simulator.h>

namespace aspect
{
    namespace Particle
    {
        // TODO: in the future, upgrade multimap to ParticleMap typedef
        // with C++11 standard "using" syntax
        
        // MPI tag for particle transfers
        const int           PARTICLE_XFER_TAG = 382;
        
        template <int dim, class T>
        class World
        {
        private:
            // Mapping for the simulation this particle world exists in
            const Mapping<dim>              *_mapping;
            
            // Triangulation for the simulation this particle world exists in
            const parallel::distributed::Triangulation<dim>   *_tria;
            
            // DoFHandler for the simulation this particle world exists in
            const DoFHandler<dim>           *_dh;
            
            // Integration scheme for moving particles in this world
            Integrator<dim, T>              *_integrator;
            
            // MPI communicator to be used for this world
            MPI_Comm                        _communicator;
            
            // If the triangulation was changed (e.g. through refinement), in which
            // case we must treat all recorded particle level/index values as invalid
            bool                            _triangulation_changed;
            
            // Set of particles currently in the local domain, organized by
            // the level/index of the cell they are in
            std::multimap<LevelInd, T>      _particles;
            
            // Total number of particles in simulation
            double                          global_sum_particles;
            
            // MPI related variables
            // MPI registered datatype encapsulating the MPI_Particle struct
            MPI_Datatype                    _particle_type;
            
            // Size and rank in the MPI communication world
            int                             _world_size;
            int                             _self_rank;
            
            // Buffers count, offset, request data for sending and receiving particles
            int                             *_num_send, *_num_recv;
            int                             _total_send, _total_recv;
            int                             *_send_offset, *_recv_offset;
            MPI_Request                     *_send_reqs, *_recv_reqs;
            
            // Generate a set of particles uniformly distributed within the specified triangulation.
            // This is done using "roulette wheel" style selection weighted by cell size.
            // We do cell-by-cell assignment of particles because the decomposition of the mesh may
            // result in a highly non-rectangular local mesh which makes uniform particle distribution difficult.
            void generate_particles_in_subdomain(unsigned int num_particles, unsigned int start_id) {
                unsigned int          i, d, v, num_tries, cur_id;
                double                total_volume, roulette_spin;
                typename parallel::distributed::Triangulation<dim>::active_cell_iterator  it;
                std::map<double, LevelInd>        roulette_wheel;
                const unsigned int n_vertices_per_cell = GeometryInfo<dim>::vertices_per_cell;
                Point<dim>            pt, max_bounds, min_bounds;
                LevelInd              select_cell;
                
                // Create the roulette wheel based on volumes of local cells
                total_volume = 0;
                for (it=_tria->begin_active(); it!=_tria->end(); ++it)
                {
                    if (it->is_locally_owned())
                    {
                        // Assign an index to each active cell for selection purposes
                        total_volume += it->measure();
                        // Save the cell index and level for later access
                        roulette_wheel.insert(std::make_pair(total_volume, std::make_pair(it->level(), it->index())));
                    }
                }
                
                // Pick cells and assign particles at random points inside them
                cur_id = start_id;
                for (i=0; i<num_particles; ++i)
                {
                    // Select a cell based on relative volume
                    roulette_spin = total_volume*drand48();
                    select_cell = roulette_wheel.lower_bound(roulette_spin)->second;
                    it = typename parallel::distributed::Triangulation<dim>::active_cell_iterator(_tria, select_cell.first, select_cell.second);
                    
                    // Get the bounds of the cell defined by the vertices
                    for (d=0; d<dim; ++d)
                    {
                        min_bounds[d] = INFINITY;
                        max_bounds[d] = -INFINITY;
                    }
                    for (v=0; v<n_vertices_per_cell; ++v)
                    {
                        pt = it->vertex(v);
                        for (d=0; d<dim; ++d)
                        {
                            min_bounds[d] = fmin(pt[d], min_bounds[d]);
                            max_bounds[d] = fmax(pt[d], max_bounds[d]);
                        }
                    }
                    
                    // Generate random points in these bounds until one is within the cell
                    num_tries = 0;
                    while (num_tries < 100)
                    {
                        for (d=0; d<dim; ++d)
                        {
                            pt[d] = drand48()*(max_bounds[d]-min_bounds[d]) + min_bounds[d];
                        }
                        try
                        {
                            if (it->point_inside(pt)) break;
                        }
                        catch (...)
                        {
                            // Debugging output, remove when Q4 mapping 3D sphere problem is resolved
                            //std::cerr << "ooo eee ooo aaa aaa " << pt << " " << select_cell.first << " " << select_cell.second << std::endl;
                            //for (int z=0;z<8;++z) std::cerr << "V" << z <<": " << it->vertex(z) << ", ";
                            //std::cerr << std::endl;
                            MPI_Abort(_communicator, 1);
                        }
                        num_tries++;
                    }
                    AssertThrow (num_tries < 100, ExcMessage ("Couldn't generate particle (unusual cell shape?)."));
                    
                    // Add the generated particle to the set
                    T new_particle(pt, cur_id);
                    _particles.insert(std::make_pair(select_cell, new_particle));
                    
                    cur_id++;
                }
            };
            
            // Recursively determines which cell the given particle belongs to
            // Returns true if the particle is in the specified cell and sets the particle
            // cell information appropriately, false otherwise
            LevelInd recursive_find_cell(T &particle, LevelInd cur_cell) {
                typename parallel::distributed::Triangulation<dim>::cell_iterator it, found_cell, child_cell;
                unsigned int    child_num;
                LevelInd        res, child_li;
                
                // If the particle is in the specified cell
                found_cell = typename parallel::distributed::Triangulation<dim>::cell_iterator(_tria, cur_cell.first, cur_cell.second);
                if (found_cell != _tria->end() && found_cell->point_inside(particle.location()))
                {
                    // If the cell is active, we're at the finest level of refinement and can finish
                    if (found_cell->active())
                    {
                        particle.set_local(found_cell->is_locally_owned());
                        return cur_cell;
                    }
                    else
                    {
                        // Otherwise we need to search deeper
                        for (child_num=0; child_num<found_cell->n_children(); ++child_num)
                        {
                            child_cell = found_cell->child(child_num);
                            child_li = LevelInd(child_cell->level(), child_cell->index());
                            res = recursive_find_cell(particle, child_li);
                            if (res.first != -1 && res.second != -1) return res;
                        }
                    }
                }
                
                // If we still can't find it, return false
                return LevelInd(-1, -1);
            };
            
            void mesh_changed(void)
            {
                _triangulation_changed = true;
            };
            
        public:
            World(void) {
                _triangulation_changed = true;
                _total_send = _total_recv = 0;
                _world_size = _self_rank = 0;
                _num_send = _num_recv = _send_offset = _recv_offset = NULL;
                _send_reqs = _recv_reqs = NULL;
                _tria = NULL;
                _mapping = NULL;
                _dh = NULL;
                _integrator = NULL;
                //MPI_Comm                        _communicator;
            };
            ~World(void) {
                if (_world_size) MPI_Type_free(&_particle_type);
                
                if (_num_send) delete _num_send;
                if (_num_recv) delete _num_recv;
                if (_send_offset) delete _send_offset;
                if (_recv_offset) delete _recv_offset;
                if (_send_reqs) delete _send_reqs;
                if (_recv_reqs) delete _recv_reqs;
            };
            
            void set_mapping(const Mapping<dim> *new_mapping) { _mapping = new_mapping; };
            void set_triangulation(const parallel::distributed::Triangulation<dim> *new_tria) {
                //if (_tria) _tria.signals.post_refinement.disconnect(std_cxx1x::bind(&World::mesh_changed, std_cxx1x::ref(*this)));
                _tria = new_tria;
                _tria->signals.post_refinement.connect(std_cxx1x::bind(&World::mesh_changed, std_cxx1x::ref(*this)));
            };
            void set_dof_handler(const DoFHandler<dim> *new_dh) { _dh = new_dh; };
            void set_integrator(Integrator<dim, T> *new_integrator) { _integrator = new_integrator; };
            void set_mpi_comm(MPI_Comm new_comm_world) { _communicator = new_comm_world; };
            const std::multimap<LevelInd, T> particles(void) const { return _particles; };
            
            // TODO: add better error checking to MPI calls
            void init(void) {
                int                                   *block_lens;
                MPI_Aint                              *indices;
                MPI_Datatype                          *old_types;
                std::vector<MPIDataInfo>              data_info;
                std::vector<MPIDataInfo>::iterator    it;
                int                                   num_entries, res, i;
                
                // Assert that all necessary parameters have been set
                
                // Construct MPI data type for this particle
                T::add_mpi_types(data_info);
                
                // And data associated with the integration scheme
                _integrator->add_mpi_types(data_info);
                
                // Set up the block lengths, indices and internal types
                num_entries = data_info.size();
                block_lens = new int[num_entries];
                indices = new MPI_Aint[num_entries];
                old_types = new MPI_Datatype[num_entries];
                for (i=0;i<num_entries;++i) {
                    block_lens[i] = data_info[i]._num_elems;
                    indices[i] = (i == 0 ? 0 : indices[i-1]+data_info[i-1]._elem_size_bytes*data_info[i-1]._num_elems);
                    old_types[i] = data_info[i]._data_type;
                }
                
                // Create and commit the MPI type
                res = MPI_Type_struct(num_entries, block_lens, indices, old_types, &_particle_type);
                if (res != MPI_SUCCESS) exit(-1);
                
                res = MPI_Type_commit(&_particle_type);
                if (res != MPI_SUCCESS) exit(-1);
                
                // Delete temporary arrays
                delete old_types;
                delete indices;
                delete block_lens;
                
                // Determine the size of the MPI comm world
                MPI_Comm_size(_communicator, &_world_size);
                MPI_Comm_rank(_communicator, &_self_rank);
                
                // Initialize send/recv structures appropriately
                _num_send = new int[_world_size];
                _num_recv = new int[_world_size];
                _send_offset = new int[_world_size];
                _recv_offset = new int[_world_size];
                _send_reqs = new MPI_Request[_world_size];
                _recv_reqs = new MPI_Request[_world_size];
            };
            
            // TODO: determine file format, write this function
            void read_particles_from_file(std::string filename);
            
            // Generate a set of particles in the current triangulation
            // TODO: fix the numbering scheme so we have exactly the right number of particles for all processor configurations
            // TODO: fix the particle system so it works even with processors assigned 0 cells
            void global_add_particles(double total_particles) {
                double      total_volume, local_volume, subdomain_fraction, start_fraction, end_fraction;
                typename parallel::distributed::Triangulation<dim>::active_cell_iterator  it;
                
                global_sum_particles = total_particles;
                // Calculate the number of particles in this domain as a fraction of total volume
                total_volume = local_volume = 0;
                for (it=_tria->begin_active(); it!=_tria->end(); ++it)
                {
                    double cell_volume = it->measure();
                    AssertThrow (cell_volume != 0, ExcMessage ("Found cell with zero volume."));
                    if (it->is_locally_owned()) local_volume += cell_volume;
                }
                
                // Sum the local volumes over all nodes
                MPI_Allreduce(&local_volume, &total_volume, 1, MPI_DOUBLE, MPI_SUM, _communicator);
                
                // Assign this subdomain the appropriate fraction
                subdomain_fraction = local_volume/total_volume;
                
                // Sum the subdomain fractions so we don't miss particles from rounding and to create unique IDs
                MPI_Scan(&subdomain_fraction, &end_fraction, 1, MPI_DOUBLE, MPI_SUM, _communicator);
                start_fraction = end_fraction-subdomain_fraction;
                
                // Calculate start and end IDs so there are no gaps
                const unsigned int  start_id = static_cast<unsigned int>(std::floor(start_fraction*total_particles));
                const unsigned int  end_id   = static_cast<unsigned int>(std::floor(end_fraction*total_particles));
                const unsigned int  subdomain_particles = end_id - start_id;
                
                generate_particles_in_subdomain(subdomain_particles, start_id);
            };
            std::string output_particle_data(const std::string &output_dir);
            void find_all_cells(void) {
                typename std::multimap<LevelInd, T>::iterator   it;
                std::multimap<LevelInd, T>                      tmp_map;
                LevelInd                                        found_cell;
                
                // Find the cells that the particles moved to
                tmp_map.clear();
                for (it=_particles.begin(); it!=_particles.end();)
                {
                    found_cell = find_cell(it->second, it->first);
                    if (found_cell != it->first)
                    {
                        tmp_map.insert(std::make_pair(found_cell, it->second));
                        _particles.erase(it++);
                    }
                    else ++it;
                }
                _particles.insert(tmp_map.begin(),tmp_map.end());
            };
            
            // Advance particles by the specified timestep using the current integration scheme.
            void advance_timestep(double timestep, const TrilinosWrappers::MPI::BlockVector &solution) {
                bool        continue_integrator = true;
                
                // Find the cells that the particles moved to
                find_all_cells();
                
                // If the triangulation changed, we may need to move particles between processors
                if (_triangulation_changed) send_recv_particles();
                
                // If particles fell out of the mesh, put them back in at the closest point in the mesh
                move_particles_back_in_mesh();
                
                // Keep calling the integrator until it indicates it is finished
                while (continue_integrator) {
                    // Starting out, particles must know which cells they belong to
                    // Using this we can quickly interpolate the velocities
                    get_particle_velocities(solution);
                    
                    // Call the integrator
                    continue_integrator = _integrator->integrate_step(_particles, timestep);
                    
                    // Find the cells that the particles moved to
                    find_all_cells();
                    
                    // If particles fell out of the mesh, put them back in at the closest point in the mesh
                    move_particles_back_in_mesh();
                    
                    // Swap particles between processors if needed
                    send_recv_particles();
                }
                
                // Ensure we didn't lose any particles
                check_particle_count();
            };
            
            void move_particles_back_in_mesh(void) {
                // TODO: fix this to work with arbitrary meshes
            };
            
            // Finds the cell the particle is contained in and returns the corresponding level/index
            LevelInd find_cell(T &particle, LevelInd cur_cell) {
                typename parallel::distributed::Triangulation<dim>::cell_iterator         it, found_cell;
                typename parallel::distributed::Triangulation<dim>::active_cell_iterator  ait;
                LevelInd    res;
                
                // First check the last recorded cell since particles will generally stay in the same area
                if (!_triangulation_changed)
                {
                    found_cell = typename parallel::distributed::Triangulation<dim>::cell_iterator(_tria, cur_cell.first, cur_cell.second);
                    if (found_cell != _tria->end() && found_cell->point_inside(particle.location()) && found_cell->active())
                    {
                        // If the cell is active, we're at the finest level of refinement and can finish
                        particle.set_local(found_cell->is_locally_owned());
                        return cur_cell;
                    }
                }
                
                // Check all the cells on level 0 and recurse down
                for (it=_tria->begin(0); it!=_tria->end(0); ++it)
                {
                    res = recursive_find_cell(particle, std::make_pair(it->level(), it->index()));
                    if (res.first != -1 && res.second != -1) return res;
                }
                
                // If we couldn't find it there, we need to check the active cells
                // since the particle could be on a curved boundary not included in the
                // coarse grid
                for (ait=_tria->begin_active(); ait!=_tria->end(); ++ait)
                {
                    if (ait->point_inside(particle.location()))
                    {
                        particle.set_local(ait->is_locally_owned());
                        return std::make_pair(ait->level(), ait->index());
                    }
                }
                
                // If it failed all these tests, the particle is outside the mesh
                particle.set_local(false);
                return std::make_pair(-1, -1);
            };
            
            /*
             Transfer particles that have crossed domain boundaries to other processors
             Because domains can change drastically during mesh refinement, particle transfer occurs as follows:
             - Each domain finds particles that have fallen outside it
             - If the new particle position is in a known domain (e.g. artificial cells), send the particle only to that domain
             - If the new particle position is not in a known domain (e.g. due to mesh refinement), send the particle to all domains
             - The position of each of these particles is broadcast to the specified domains
             - Each domain determines which of the broadcast particles is in itself, keeps these and deletes the others
             - TODO: handle particles outside any domain
             - TODO: if we know the domain of a particle (e.g. bordering domains), send it only to that domain
             */
            void send_recv_particles(void) {
                typename std::multimap<LevelInd, T>::iterator  it;
                typename parallel::distributed::Triangulation<dim>::cell_iterator found_cell;
                int                 i, rank;
                std::vector<T>      send_particles;
                typename std::vector<T>::const_iterator    sit;
                char                *send_data, *recv_data, *cur_send_ptr, *cur_recv_ptr;
                unsigned int        integrator_data_len, particle_data_len;
                
                // Go through the particles and take out those which need to be moved to another processor
                for (it=_particles.begin(); it!=_particles.end();)
                {
                    if (!it->second.local())
                    {
                        send_particles.push_back(it->second);
                        _particles.erase(it++);
                    }
                    else
                    {
                        ++it;
                    }
                }
                
                // Determine the total number of particles we will send to other processors
                _total_send = send_particles.size();
                for (rank=0; rank<_world_size; ++rank)
                {
                    if (rank != _self_rank) _num_send[rank] = _total_send;
                    else _num_send[rank] = 0;
                    _send_offset[rank] = 0;
                }
                
                // Notify other processors how many particles we will be sending
                MPI_Alltoall(_num_send, 1, MPI_INT, _num_recv, 1, MPI_INT, _communicator);
                
                _total_recv = 0;
                for (rank=0; rank<_world_size; ++rank)
                {
                    _recv_offset[rank] = _total_recv;
                    _total_recv += _num_recv[rank];
                }
                
                // Allocate space for sending and receiving particle data
                integrator_data_len = _integrator->data_len(MPI_DATA);
                particle_data_len = T::data_len(MPI_DATA);
                send_data = (char *)malloc(_total_send*(integrator_data_len+particle_data_len));
                recv_data = (char *)malloc(_total_recv*(integrator_data_len+particle_data_len));
                
                // Copy the particle data into the send array
                // TODO: add integrator data
                cur_send_ptr = send_data;
                for (i=0,sit=send_particles.begin(); sit!=send_particles.end(); ++sit,++i)
                {
                    cur_send_ptr = sit->write_data(MPI_DATA, cur_send_ptr);
                    cur_send_ptr = _integrator->write_data(MPI_DATA, sit->id_num(), cur_send_ptr);
                }
                
                // Exchange the particle data between domains
                MPI_Alltoallv(send_data, _num_send, _send_offset, _particle_type,
                              recv_data, _num_recv, _recv_offset, _particle_type,
                              _communicator);
                
                int put_in_domain = 0;
                // Put the received particles into the domain if they are in the triangulation
                cur_recv_ptr = recv_data;
                for (i=0; i<_total_recv; ++i)
                {
                    T                   recv_particle;
                    LevelInd            found_cell;
                    cur_recv_ptr = recv_particle.read_data(MPI_DATA, cur_recv_ptr);
                    cur_recv_ptr = _integrator->read_data(MPI_DATA, recv_particle.id_num(), cur_recv_ptr);
                    found_cell = find_cell(recv_particle, std::make_pair(-1,-1));
                    if (recv_particle.local())
                    {
                        put_in_domain++;
                        _particles.insert(std::make_pair(found_cell, recv_particle));
                    }
                }
                
                free(send_data);
                free(recv_data);
            };
            
            void get_particle_velocities(const TrilinosWrappers::MPI::BlockVector &solution) {
                Vector<double>                single_res(dim+2);
                std::vector<Vector<double> >  result;
                Point<dim>                    velocity;
                unsigned int                  i, num_cell_particles;
                LevelInd                      cur_cell;
                typename std::multimap<LevelInd, T>::iterator  it, sit;
                typename DoFHandler<dim>::active_cell_iterator  found_cell;
                std::vector<Point<dim> >      particle_points;
                
                // Prepare the field function
                Functions::FEFieldFunction<dim, DoFHandler<dim>, TrilinosWrappers::MPI::BlockVector> fe_value(*_dh, solution, *_mapping);
                
                // Get the velocity for each cell at a time so we can take advantage of knowing the active cell
                for (it=_particles.begin(); it!=_particles.end();)
                {
                    // Save a pointer to the first particle in this cell
                    sit = it;
                    
                    // Get the current cell
                    cur_cell = it->first;
                    
                    // Resize the vectors to the number of particles in this cell
                    num_cell_particles = _particles.count(cur_cell);
                    particle_points.resize(num_cell_particles);
                    result.resize(num_cell_particles, single_res);
                    
                    // Get a vector of the point positions in this cell
                    i=0;
                    while (it != _particles.end() && it->first == cur_cell)
                    {
                        particle_points[i++] = it->second.location();
                        it++;
                    }
                    
                    // Get the cell the particle is in
                    found_cell = typename DoFHandler<dim>::active_cell_iterator(_tria, cur_cell.first, cur_cell.second, _dh);
                    
                    // Interpolate the velocity field for each of the particles
                    fe_value.set_active_cell(found_cell);
                    fe_value.vector_value_list(particle_points, result);
                    
                    // Copy the resulting velocities to the appropriate vector
                    it = sit;
                    for (i=0; i<num_cell_particles; ++i)
                    {
                        for (int d=0; d<dim; ++d) velocity(d) = result[i](d);
                        it->second.set_velocity(velocity);
                        it++;
                    }
                }
            };
            
            unsigned int get_global_particle_count(void) {
                unsigned int    local_particles = _particles.size();
                unsigned int    global_particles;
                int             res;
                
                res = MPI_Allreduce(&local_particles, &global_particles, 1, MPI_UNSIGNED, MPI_SUM, _communicator);
                if (res != MPI_SUCCESS) exit(-1);
                return global_particles;
            };
            
            // Ensures that particles are not lost in the simulation
            void check_particle_count(void) {
                unsigned int    global_particles = get_global_particle_count();
                
                AssertThrow (global_particles==global_sum_particles, ExcMessage ("Particle count unexpectedly changed."));
            };
        };
    }
}

#endif
