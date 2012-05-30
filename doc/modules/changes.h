/**
 * @page changes_after_0_1 Changes after Version 0.1

<p>
This is the list of changes made after the release of
Aspect version 0.1.
All entries are signed with the names of the author.
</p>



<ol>
<li>
New: Aspect now writes a <code>solution.pvd</code> for Paraview
that contains a list of the files that jointly make up the entire
simulation (and not just a single time step) together with the
simulation time each of the files that describe a time step correspond
to.
<br>
(Wolfgang Bangerth, 2012/05/30)

<li>
New: The variables we output in graphical format files are now
user selectable from the input parameter file. Functions that
compute something from velocity, pressure and temperature (e.g.,
the viscosity, strain rate, etc) are now implemented as plugins
like many of the other parts of Aspect.
now implemented
<br>
(Wolfgang Bangerth, 2012/05/08)

<li>
Modified: Checkpoint frequency is now a user specified parameter.
Frequency may be specified as either wall time or number of time steps.
<br>
(Eric Heien, 2012/05/04)

<li>
Modified: Background writing of visualization data now uses the
mkstemp function rather than forking mktemp, and will continue as
normal (at lower efficiency) if temporary files cannot be created.
<br>
(Eric Heien, 2012/05/03)

<li>
New: The compressibility that functions implementing the interface
aspect::MaterialModel::Interface::compressibility() need to return was
previously defined incorrectly as $\frac{\partial\rho}{\partial p}$.
This is not what is commonly referred to as compressibility, and
the function is now supposed to return
$\frac 1\rho \frac{\partial\rho}{\partial p}$ instead, following the
common definition of the word.
<br>
Note that all currently implemented compressible models already did
that.
<br>
(Wolfgang Bangerth, Timo Heister, 2012/05/03)

<li>
New: The number of space dimensions in which a simulation happens is now
a parameter that is set in the input parameter filer, rather than
statically at compile time as before.
<br>
(Wolfgang Bangerth, 2012/04/03)

<li>
New: A postprocessor module for particles was added. This module creates
a random uniform distribution of particles in the mesh and moves them based
on the velocity field using an RK2 scheme.
<br>
(Eric Heien, 2012/04/02)
</ol>


*/
