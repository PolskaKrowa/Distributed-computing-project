! tests/fortran/test_root_find.f90
!
! Test suite for root finding kernels
! Tests Newton-Raphson and Secant methods on various functions
!
program test_root_find
    use kinds, only: wp
    use constants, only: PI, E
    use newton_raphson
    use secant
    implicit none
    
    integer :: total_tests, passed_tests
    
    total_tests = 0
    passed_tests = 0
    
    print *, "================================================"
    print *, "Root Finding Kernels Test Suite"
    print *, "================================================"
    print *, ""
    
    ! Newton-Raphson tests
    call test_nr_polynomial(total_tests, passed_tests)
    call test_nr_transcendental(total_tests, passed_tests)
    call test_nr_bracket(total_tests, passed_tests)
    
    ! Secant method tests
    call test_secant_polynomial(total_tests, passed_tests)
    call test_secant_transcendental(total_tests, passed_tests)
    call test_secant_bracket(total_tests, passed_tests)
    
    ! Edge cases
    call test_edge_cases(total_tests, passed_tests)
    
    ! Summary
    print *, ""
    print *, "================================================"
    print *, "Test Summary"
    print *, "================================================"
    print "(A,I0,A,I0)", " Passed: ", passed_tests, " / ", total_tests
    if (passed_tests == total_tests) then
        print *, " Result: ALL TESTS PASSED ✓"
    else
        print "(A,I0,A)", " Result: ", total_tests - passed_tests, " test(s) FAILED ✗"
    end if
    print *, "================================================"
    
contains

    !> Test Newton-Raphson on polynomial: f(x) = x^2 - 2 (root at x = sqrt(2))
    subroutine test_nr_polynomial(total, passed)
        integer, intent(inout) :: total, passed
        real(wp) :: root, expected
        integer :: status, num_iter
        
        total = total + 1
        print *, "Test 1: Newton-Raphson on x^2 - 2"
        
        expected = sqrt(2.0_wp)
        call newton_raphson_solve(poly_f, poly_df, 1.0_wp, root, status, num_iter=num_iter)
        
        print "(A,F16.12)", "  Expected root: ", expected
        print "(A,F16.12)", "  Computed root: ", root
        print "(A,I0)", "  Status: ", status
        print "(A,I0)", "  Iterations: ", num_iter
        
        if (status == NR_SUCCESS .and. abs(root - expected) < 1.0e-10_wp) then
            print *, "  Result: PASS ✓"
            passed = passed + 1
        else
            print *, "  Result: FAIL ✗"
        end if
        print *, ""
    end subroutine test_nr_polynomial
    
    !> Test Newton-Raphson on transcendental: f(x) = cos(x) (root at x = π/2)
    subroutine test_nr_transcendental(total, passed)
        integer, intent(inout) :: total, passed
        real(wp) :: root, expected
        integer :: status, num_iter
        
        total = total + 1
        print *, "Test 2: Newton-Raphson on cos(x)"
        
        expected = PI / 2.0_wp
        call newton_raphson_solve(cos_f, cos_df, 1.5_wp, root, status, num_iter=num_iter)
        
        print "(A,F16.12)", "  Expected root: ", expected
        print "(A,F16.12)", "  Computed root: ", root
        print "(A,I0)", "  Status: ", status
        print "(A,I0)", "  Iterations: ", num_iter
        
        if (status == NR_SUCCESS .and. abs(root - expected) < 1.0e-10_wp) then
            print *, "  Result: PASS ✓"
            passed = passed + 1
        else
            print *, "  Result: FAIL ✗"
        end if
        print *, ""
    end subroutine test_nr_transcendental
    
    !> Test Newton-Raphson with bracketing
    subroutine test_nr_bracket(total, passed)
        integer, intent(inout) :: total, passed
        real(wp) :: root, expected
        integer :: status, num_iter
        
        total = total + 1
        print *, "Test 3: Newton-Raphson with bracketing on x^3 - x - 2"
        
        expected = 1.521379706804568_wp  ! Exact root
        call newton_raphson_solve_bracket(cubic_f, cubic_df, 1.0_wp, 2.0_wp, &
                                          root, status, num_iter=num_iter)
        
        print "(A,F16.12)", "  Expected root: ", expected
        print "(A,F16.12)", "  Computed root: ", root
        print "(A,I0)", "  Status: ", status
        print "(A,I0)", "  Iterations: ", num_iter
        
        if (status == NR_SUCCESS .and. abs(root - expected) < 1.0e-10_wp) then
            print *, "  Result: PASS ✓"
            passed = passed + 1
        else
            print *, "  Result: FAIL ✗"
        end if
        print *, ""
    end subroutine test_nr_bracket
    
    !> Test Secant method on polynomial
    subroutine test_secant_polynomial(total, passed)
        integer, intent(inout) :: total, passed
        real(wp) :: root, expected
        integer :: status, num_iter
        
        total = total + 1
        print *, "Test 4: Secant method on x^2 - 2"
        
        expected = sqrt(2.0_wp)
        call secant_solve(poly_f, 1.0_wp, 2.0_wp, root, status, num_iter=num_iter)
        
        print "(A,F16.12)", "  Expected root: ", expected
        print "(A,F16.12)", "  Computed root: ", root
        print "(A,I0)", "  Status: ", status
        print "(A,I0)", "  Iterations: ", num_iter
        
        if (status == SEC_SUCCESS .and. abs(root - expected) < 1.0e-10_wp) then
            print *, "  Result: PASS ✓"
            passed = passed + 1
        else
            print *, "  Result: FAIL ✗"
        end if
        print *, ""
    end subroutine test_secant_polynomial
    
    !> Test Secant method on transcendental
    subroutine test_secant_transcendental(total, passed)
        integer, intent(inout) :: total, passed
        real(wp) :: root, expected
        integer :: status, num_iter
        
        total = total + 1
        print *, "Test 5: Secant method on cos(x)"
        
        expected = PI / 2.0_wp
        call secant_solve(cos_f, 1.5_wp, 1.6_wp, root, status, num_iter=num_iter)
        
        print "(A,F16.12)", "  Expected root: ", expected
        print "(A,F16.12)", "  Computed root: ", root
        print "(A,I0)", "  Status: ", status
        print "(A,I0)", "  Iterations: ", num_iter
        
        if (status == SEC_SUCCESS .and. abs(root - expected) < 1.0e-10_wp) then
            print *, "  Result: PASS ✓"
            passed = passed + 1
        else
            print *, "  Result: FAIL ✗"
        end if
        print *, ""
    end subroutine test_secant_transcendental
    
    !> Test Secant method with bracketing
    subroutine test_secant_bracket(total, passed)
        integer, intent(inout) :: total, passed
        real(wp) :: root, expected
        integer :: status, num_iter
        
        total = total + 1
        print *, "Test 6: Secant method with bracketing on x^3 - x - 2"
        
        expected = 1.521379706804568_wp
        call secant_solve_bracket(cubic_f, 1.0_wp, 2.0_wp, root, status, num_iter=num_iter)
        
        print "(A,F16.12)", "  Expected root: ", expected
        print "(A,F16.12)", "  Computed root: ", root
        print "(A,I0)", "  Status: ", status
        print "(A,I0)", "  Iterations: ", num_iter
        
        if (status == SEC_SUCCESS .and. abs(root - expected) < 1.0e-10_wp) then
            print *, "  Result: PASS ✓"
            passed = passed + 1
        else
            print *, "  Result: FAIL ✗"
        end if
        print *, ""
    end subroutine test_secant_bracket
    
    !> Test edge cases and error handling
    subroutine test_edge_cases(total, passed)
        integer, intent(inout) :: total, passed
        real(wp) :: root
        integer :: status
        
        ! Test 7: Newton-Raphson with zero derivative
        total = total + 1
        print *, "Test 7: Newton-Raphson with zero derivative"
        call newton_raphson_solve(flat_f, flat_df, 1.0_wp, root, status)
        print "(A,I0)", "  Status: ", status
        if (status == NR_ZERO_DERIVATIVE) then
            print *, "  Result: PASS ✓ (correctly detected zero derivative)"
            passed = passed + 1
        else
            print *, "  Result: FAIL ✗"
        end if
        print *, ""
        
        ! Test 8: Secant with invalid bracket
        total = total + 1
        print *, "Test 8: Secant with invalid bracket (same sign)"
        call secant_solve_bracket(poly_f, 2.0_wp, 3.0_wp, root, status)
        print "(A,I0)", "  Status: ", status
        if (status == SEC_INVALID_INPUT) then
            print *, "  Result: PASS ✓ (correctly rejected invalid bracket)"
            passed = passed + 1
        else
            print *, "  Result: FAIL ✗"
        end if
        print *, ""
        
        ! Test 9: Finding exact root (x = 0 for f(x) = x)
        total = total + 1
        print *, "Test 9: Finding exact root at x = 0"
        call secant_solve(identity_f, -0.1_wp, 0.1_wp, root, status)
        print "(A,F16.12)", "  Computed root: ", root
        print "(A,I0)", "  Status: ", status
        if (status == SEC_SUCCESS .and. abs(root) < 1.0e-10_wp) then
            print *, "  Result: PASS ✓"
            passed = passed + 1
        else
            print *, "  Result: FAIL ✗"
        end if
        print *, ""
    end subroutine test_edge_cases
    
    ! ========================================================================
    ! Test Functions
    ! ========================================================================
    
    ! f(x) = x^2 - 2
    pure function poly_f(x) result(f)
        real(wp), intent(in) :: x
        real(wp) :: f
        f = x**2 - 2.0_wp
    end function poly_f
    
    pure function poly_df(x) result(df)
        real(wp), intent(in) :: x
        real(wp) :: df
        df = 2.0_wp * x
    end function poly_df
    
    ! f(x) = cos(x)
    pure function cos_f(x) result(f)
        real(wp), intent(in) :: x
        real(wp) :: f
        f = cos(x)
    end function cos_f
    
    pure function cos_df(x) result(df)
        real(wp), intent(in) :: x
        real(wp) :: df
        df = -sin(x)
    end function cos_df
    
    ! f(x) = x^3 - x - 2
    pure function cubic_f(x) result(f)
        real(wp), intent(in) :: x
        real(wp) :: f
        f = x**3 - x - 2.0_wp
    end function cubic_f
    
    pure function cubic_df(x) result(df)
        real(wp), intent(in) :: x
        real(wp) :: df
        df = 3.0_wp * x**2 - 1.0_wp
    end function cubic_df
    
    ! f(x) = 5 (constant function - zero derivative everywhere)
    pure function flat_f(x) result(f)
        real(wp), intent(in) :: x
        real(wp) :: f
        f = 5.0_wp
    end function flat_f
    
    pure function flat_df(x) result(df)
        real(wp), intent(in) :: x
        real(wp) :: df
        df = 0.0_wp
    end function flat_df
    
    ! f(x) = x (root at origin)
    pure function identity_f(x) result(f)
        real(wp), intent(in) :: x
        real(wp) :: f
        f = x
    end function identity_f

end program test_root_find