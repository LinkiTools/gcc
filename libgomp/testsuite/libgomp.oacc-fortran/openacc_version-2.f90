! { dg-do run }

program main
  use openacc
  implicit none

  if (openacc_version .ne. 201510) call abort;

end program main
