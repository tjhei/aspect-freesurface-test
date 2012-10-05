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

#ifndef __aspect__particle_integrator_h
#define __aspect__particle_integrator_h

#include <aspect/particle/particle.h>
#include <aspect/simulator.h>

namespace aspect
{
    namespace Particle
    {
        // Integrator is an abstract class defining virtual methods for performing integration of
        // particle paths through the simulation velocity field
        template <int dim, class T>
        class Integrator {
        public:
            // Perform an integration step of moving the particles by the specified timestep dt.
            // Implementations of this function should update the particle location.
            // If the integrator requires multiple internal steps, this should return true until
            // all internal steps are finished.  Between calls to this, the particles will be moved
            // based on the velocities at their newly specified positions.
            virtual bool integrate_step(std::multimap<LevelInd, T> &particles, double dt) = 0;
            
            // Specify the MPI types and data sizes involved in transferring integration related
            // information between processes. If the integrator samples velocities at different
            // locations and the particle moves between processes during the integration step,
            // the sampled velocities must be transferred with the particle.
            virtual void add_mpi_types(std::vector<MPIDataInfo> &data_info) = 0;
            
            // Must reutnr data length in bytes of the integration related data
            // for a particle in a given format.
            virtual unsigned int data_len(ParticleDataFormat format) const = 0;
            // Read integration related data for a particle specified by id_num
            // Returns the data pointer updated to point to the next unwritten byte
            virtual char *read_data(ParticleDataFormat format, double id_num, char *data) = 0;
            // Write integration related data for a particle specified by id_num
            // Returns the data pointer updated to point to the next unwritten byte
            virtual char *write_data(ParticleDataFormat format, double id_num, char *data) = 0;
        };
        
        // Euler scheme integrator, where y_{n+1} = y_n + dt * v(y_n)
        // This requires only one step per integration, and doesn't involve any extra data.
        template <int dim, class T>
        class EulerIntegrator : public Integrator<dim, T> {
        public:
            virtual bool integrate_step(std::multimap<LevelInd, T> &particles, double dt) {
                typename std::multimap<LevelInd, T>::iterator       it;
                Point<dim>                          loc, vel;
                
                for (it=particles.begin();it!=particles.end();++it) {
                    loc = it->second.location();
                    vel = it->second.velocity();
                    it->second.set_location(loc + dt*vel);
                }
                
                return false;
            };
            virtual void add_mpi_types(std::vector<MPIDataInfo> &data_info) {};
            virtual unsigned int data_len(ParticleDataFormat format) const { return 0; };
            virtual char *read_data(ParticleDataFormat format, double id_num, char *data) { return data; };
            virtual char *write_data(ParticleDataFormat format, double id_num, char *data) { return data; };
        };
        
        // Runge Kutta second order integrator, where y_{n+1} = y_n + dt*f(k_1, k_1 = v(
        // This scheme requires storing the original location, and the read/write_data functions reflect this
        template <int dim, class T>
        class RK2Integrator : public Integrator<dim, T> {
        private:
            unsigned int                    _step;
            std::map<double, Point<dim> >   _loc0;
            
        public:
            RK2Integrator(void) {
                _step = 0;
                _loc0.clear();
            };
            
            virtual bool integrate_step(std::multimap<LevelInd, T> &particles, double dt) {
                typename std::multimap<LevelInd, T>::iterator       it;
                Point<dim>                          loc, vel;
                double                              id_num;
                
                for (it=particles.begin();it!=particles.end();++it) {
                    id_num = it->second.id_num();
                    loc = it->second.location();
                    vel = it->second.velocity();
                    if (_step == 0) {
                        _loc0[id_num] = loc;
                        it->second.set_location(loc + 0.5*dt*vel);
                    } else if (_step == 1) {
                        it->second.set_location(_loc0[id_num] + dt*vel);
                    } else {
                        // Error!
                    }
                }
                
                if (_step == 1) _loc0.clear();
                _step = (_step+1)%2;
                
                // Continue until we're at the last step
                return (_step != 0);
            };
            
            virtual void add_mpi_types(std::vector<MPIDataInfo> &data_info) {
                // Add the _loc0 data
                data_info.push_back(MPIDataInfo("loc0", dim, MPI_DOUBLE, sizeof(double)));
            };
            
            virtual unsigned int data_len(ParticleDataFormat format) const {
                switch (format) {
                    case MPI_DATA:
                    case HDF5_DATA:
                        return dim*sizeof(double);
                }
                return 0;
            };
            
            virtual char *read_data(ParticleDataFormat format, double id_num, char *data) {
                char            *p = data;
                unsigned int    i;
                
                switch (format) {
                    case MPI_DATA:
                    case HDF5_DATA:
                        // Read location data
                        for (i=0;i<dim;++i) { _loc0[id_num](i) = ((double *)p)[0]; p += sizeof(double); }
                        break;
                }
                
                return p;
            };
            
            virtual char *write_data(ParticleDataFormat format, double id_num, char *data) {
                char          *p = data;
                unsigned int  i;
                
                // Then write our data in the appropriate format
                switch (format) {
                    case MPI_DATA:
                    case HDF5_DATA:
                        // Write location data
                        for (i=0;i<dim;++i) { ((double *)p)[0] = _loc0[id_num](i); p += sizeof(double); }
                        break;
                }
                
                return p;
            };
        };

        
        template <int dim, class T>
        class RK4Integrator : public Integrator<dim, T> {
        private:
            unsigned int                    _step;
            std::map<double, Point<dim> >   _loc0, _k1, _k2, _k3;
            
        public:
            RK4Integrator(void) {
                _step = 0;
                _loc0.clear();
                _k1.clear();
                _k2.clear();
                _k3.clear();
            };
            
            virtual bool integrate_step(std::multimap<LevelInd, T> &particles, double dt) {
                typename std::multimap<LevelInd, T>::iterator       it;
                Point<dim>                          loc, vel, k4;
                double                              id_num;
                
                for (it=particles.begin();it!=particles.end();++it) {
                    id_num = it->second.id_num();
                    loc = it->second.location();
                    vel = it->second.velocity();
                    if (_step == 0) {
                        _loc0[id_num] = loc;
                        _k1[id_num] = dt*vel;
                        it->second.set_location(loc + 0.5*_k1[id_num]);
                    } else if (_step == 1) {
                        _k2[id_num] = dt*vel;
                        it->second.set_location(_loc0[id_num] + 0.5*_k2[id_num]);
                    } else if (_step == 2) {
                        _k3[id_num] = dt*vel;
                        it->second.set_location(_loc0[id_num] + _k3[id_num]);
                    } else if (_step == 3) {
                        k4 = dt*vel;
                        it->second.set_location(_loc0[id_num] + (_k1[id_num]+2*_k2[id_num]+2*_k3[id_num]+k4)/6.0);
                    } else {
                        // Error!
                    }
                }
                
                _step = (_step+1)%4;
                if (_step == 0) {
                    _loc0.clear();
                    _k1.clear();
                    _k2.clear();
                    _k3.clear();
                }
                
                // Continue until we're at the last step
                return (_step != 0);
            };
            
            virtual void add_mpi_types(std::vector<MPIDataInfo> &data_info) {
                // Add the _loc0, _k1, _k2, and _k3 data
                data_info.push_back(MPIDataInfo("loc0", dim, MPI_DOUBLE, sizeof(double)));
                data_info.push_back(MPIDataInfo("k1", dim, MPI_DOUBLE, sizeof(double)));
                data_info.push_back(MPIDataInfo("k2", dim, MPI_DOUBLE, sizeof(double)));
                data_info.push_back(MPIDataInfo("k3", dim, MPI_DOUBLE, sizeof(double)));
            };
            
            virtual unsigned int data_len(ParticleDataFormat format) const {
                switch (format) {
                    case MPI_DATA:
                    case HDF5_DATA:
                        return 4*dim*sizeof(double);
                }
                return 0;
            };
            
            virtual char *read_data(ParticleDataFormat format, double id_num, char *data) {
                char            *p = data;
                unsigned int    i;
                
                switch (format) {
                    case MPI_DATA:
                    case HDF5_DATA:
                        // Read location data
                        for (i=0;i<dim;++i) { _loc0[id_num](i) = ((double *)p)[0]; p += sizeof(double); }
                        // Read k1, k2 and k3
                        for (i=0;i<dim;++i) { _k1[id_num](i) = ((double *)p)[0]; p += sizeof(double); }
                        for (i=0;i<dim;++i) { _k2[id_num](i) = ((double *)p)[0]; p += sizeof(double); }
                        for (i=0;i<dim;++i) { _k3[id_num](i) = ((double *)p)[0]; p += sizeof(double); }
                        break;
                }
                
                return p;
            };
            
            virtual char *write_data(ParticleDataFormat format, double id_num, char *data) {
                char          *p = data;
                unsigned int  i;
                
                // Then write our data in the appropriate format
                switch (format) {
                    case MPI_DATA:
                    case HDF5_DATA:
                        // Write location data
                        for (i=0;i<dim;++i) { ((double *)p)[0] = _loc0[id_num](i); p += sizeof(double); }
                        // Write k1, k2 and k3
                        for (i=0;i<dim;++i) { ((double *)p)[0] = _k1[id_num](i); p += sizeof(double); }
                        for (i=0;i<dim;++i) { ((double *)p)[0] = _k2[id_num](i); p += sizeof(double); }
                        for (i=0;i<dim;++i) { ((double *)p)[0] = _k3[id_num](i); p += sizeof(double); }
                        break;
                }
                
                return p;
            };
        };
    }
}

#endif
