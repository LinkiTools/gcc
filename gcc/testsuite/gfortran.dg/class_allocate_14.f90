! { dg-do run }
! { dg-options "-fdump-tree-original" }
!
! PR fortran/56845
!
module m
type t
integer ::a
end type t
contains
subroutine sub
  type(t), save, allocatable :: x
  class(t), save,allocatable :: y
  if (.not. same_type_as(x,y)) call abort()
end subroutine sub
subroutine sub2
  type(t), save, allocatable :: a(:)
  class(t), save,allocatable :: b(:)
  if (.not. same_type_as(a,b)) call abort()
end subroutine sub2
end module m

use m
call sub()
call sub2()
end

! { dg-final { scan-tree-dump-times "static struct __class_m_T_1_0a b = {._data={.base_addr=0B, .elem_len=4, .version=1, .rank=1, .attribute=2, .type=-1}, ._vptr=&__vtab_m_T};" 1 "original" } }
! { dg-final { scan-tree-dump-times "static struct __class_m_T_a y = {._data=0B, ._vptr=&__vtab_m_T};" 1 "original" } }
<<<<<<< .working

! { dg-final { cleanup-tree-dump "original" } }
||||||| .merge-left.r222315
! { dg-final { cleanup-tree-dump "original" } }
=======
>>>>>>> .merge-right.r235035

