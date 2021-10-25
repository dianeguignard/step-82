# step-82

In this deal.ii tutorial program, we consider the local discontinuous Galerkin (LDG) method
for approximating the solution to a bi-Laplacian problem. The discrete problem is obtained,
up to stabilization terms, by simply replacing the Hessians by discrete (aka reconstructed)
Hessians. Such discrete operator consists of three distinct parts: the broken Hessian, the
lifting of the jumps of the broken gradient of the function and the lifting of the jumps of
the function itseft. We refer to

https://www.dealii.org/developer/doxygen/deal.II/step_82.html

for details.

<h3> Running the program </h3>
Provided you have a recent version of deal.II installed, you can configure, compile, and run this program using the following commands:

$ cmake -DDEAL_II_DIR=/path/to/dealii . </br>
$ make </br>
$ make run
