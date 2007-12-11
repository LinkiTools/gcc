! { dg-do run }

program tasktest
  use omp_lib
  integer :: i, j
  common /tasktest_j/ j
  j = 0
  !$omp parallel private (i)
    i = omp_get_thread_num ()
    if (i.lt.2) then
      !$omp task untied default(firstprivate)
        call subr (i + 1)
      !$omp end task
    end if
    !$omp taskwait
  !$omp end parallel
  if (j.gt.0) call abort
contains
  subroutine subr (i)
    use omp_lib
    integer :: i, j
    common /tasktest_j/ j
    if (omp_get_thread_num ().ne.(i - 1)) then
    !$omp atomic
      j = j + 1
    end if
  end subroutine subr
end program tasktest
