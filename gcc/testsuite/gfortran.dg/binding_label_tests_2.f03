! { dg-do compile }
module binding_label_tests_2

contains
  ! this is just here so at least one of the subroutines will be accepted so
  ! gfortran doesn't give an Extension warning when using -pedantic-errors
  subroutine ok() 
  end subroutine ok

  subroutine sub0() bind(c, name="   1") ! { dg-error "Syntax error in BIND.C." }
  end subroutine sub0 ! { dg-error "Expecting END MODULE" }

  subroutine sub1() bind(c, name="$") ! { dg-error "Syntax error in BIND.C." }
  end subroutine sub1 ! { dg-error "Expecting END MODULE" }

  subroutine sub2() bind(c, name="abc$") ! { dg-error "Syntax error in BIND.C." }
  end subroutine sub2 ! { dg-error "Expecting END MODULE" }

  subroutine sub3() bind(c, name="abc d") ! { dg-error "Syntax error in BIND.C." }
  end subroutine sub3 ! { dg-error "Expecting END MODULE" }

  ! nothing between the quotes except spaces, so name="".
  ! this should be an error if the procedure is not a dummy or procedure
  ! pointer
  subroutine sub4() BIND(c, name = "        ") ! { dg-error "in BIND.C." }
  end subroutine sub4 ! { dg-error "Expecting END MODULE" }
end module binding_label_tests_2 
