! tests/fortran/test_core.f90
!
! Comprehensive test programme for Fortran core modules.
! Verifies functionality of kinds, constants, rng, and numerics_utils.
!
! Build:
!   gfortran -o test_core kinds.f90 constants.f90 rng.f90 \
!            numerics_utils.f90 test_core.f90
!
! Run:
!   ./test_core
!
program test_core
    use kinds
    use constants
    use rng
    use numerics_utils
    implicit none
    
    integer :: failures
    
    print '(A)', "=========================================="
    print '(A)', "Testing Fortran Core Modules"
    print '(A)', "=========================================="
    print *
    
    failures = 0
    
    ! Test each module
    call test_kinds(failures)
    call test_constants(failures)
    call test_rng(failures)
    call test_numerics(failures)
    
    ! Summary
    print *
    print '(A)', "=========================================="
    if (failures == 0) then
        print '(A)', "✓ ALL TESTS PASSED"
    else
        print '(A,I0,A)', "✗ ", failures, " TESTS FAILED"
    end if
    print '(A)', "=========================================="
    
    if (failures > 0) stop 1
    
contains

    subroutine test_kinds(failures)
        integer, intent(inout) :: failures
        real(sp) :: x_sp
        real(dp) :: x_dp
        integer(i32) :: i_32
        integer(i64) :: i_64
        
        print '(A)', "Testing kinds module..."
        
        ! Verify kind parameters
        if (sp /= real32) then
            print '(A)', "  ✗ Single precision kind incorrect"
            failures = failures + 1
        else
            print '(A)', "  ✓ Single precision: 32-bit"
        end if
        
        if (dp /= real64) then
            print '(A)', "  ✗ Double precision kind incorrect"
            failures = failures + 1
        else
            print '(A)', "  ✓ Double precision: 64-bit"
        end if
        
        ! Test epsilon
        if (get_epsilon(dp) /= epsilon(1.0_dp)) then
            print '(A)', "  ✗ get_epsilon failed"
            failures = failures + 1
        else
            print '(A,ES10.3)', "  ✓ Machine epsilon: ", get_epsilon(dp)
        end if
        
        print *
    end subroutine test_kinds
    
    subroutine test_constants(failures)
        integer, intent(inout) :: failures
        real(wp) :: x, y
        
        print '(A)', "Testing constants module..."
        
        ! Test mathematical constants
        x = sin(HALF_PI)
        if (.not. nearly_equal(x, 1.0_wp, 1.0e-10_wp)) then
            print '(A)', "  ✗ HALF_PI constant incorrect"
            failures = failures + 1
        else
            print '(A)', "  ✓ sin(π/2) = 1"
        end if
        
        x = exp(1.0_wp)
        if (.not. nearly_equal(x, E, 1.0e-10_wp)) then
            print '(A)', "  ✗ E constant incorrect"
            failures = failures + 1
        else
            print '(A,F12.9)', "  ✓ e = ", E
        end if
        
        ! Test angle conversion
        x = 180.0_wp * DEG_TO_RAD
        if (.not. nearly_equal(x, PI, 1.0e-10_wp)) then
            print '(A)', "  ✗ Angle conversion failed"
            failures = failures + 1
        else
            print '(A)', "  ✓ 180° = π radians"
        end if
        
        ! Test nearly_equal
        if (.not. nearly_equal(1.0_wp, 1.0_wp + 1.0e-14_wp)) then
            print '(A)', "  ✗ nearly_equal failed"
            failures = failures + 1
        else
            print '(A)', "  ✓ Floating-point comparison works"
        end if
        
        ! Test is_zero
        if (.not. is_zero(1.0e-15_wp)) then
            print '(A)', "  ✗ is_zero failed"
            failures = failures + 1
        else
            print '(A)', "  ✓ Zero detection works"
        end if
        
        print *
    end subroutine test_constants
    
    subroutine test_rng(failures)
        integer, intent(inout) :: failures
        real(wp) :: x, y
        real(wp) :: samples(1000)
        real(wp) :: mean_val, std_val
        integer :: i
        
        print '(A)', "Testing rng module..."
        
        ! Test seeding
        call rng_seed(42_i64)
        x = rng_uniform()
        call rng_seed(42_i64)  ! Same seed
        y = rng_uniform()
        
        if (.not. nearly_equal(x, y, 1.0e-10_wp)) then
            print '(A)', "  ✗ Deterministic seeding failed"
            failures = failures + 1
        else
            print '(A)', "  ✓ RNG seeding is deterministic"
        end if
        
        ! Test uniform distribution
        call rng_seed(123_i64)
        call rng_uniform_array(samples)
        
        mean_val = sum(samples) / size(samples)
        if (abs(mean_val - 0.5_wp) > 0.05_wp) then
            print '(A)', "  ✗ Uniform distribution mean incorrect"
            failures = failures + 1
        else
            print '(A,F8.5)', "  ✓ Uniform mean ≈ 0.5: ", mean_val
        end if
        
        ! Test normal distribution
        call rng_seed(456_i64)
        call rng_normal_array(samples, mean=0.0_wp, sigma=1.0_wp)
        
        mean_val = sum(samples) / size(samples)
        std_val = sqrt(sum((samples - mean_val)**2) / size(samples))
        
        if (abs(mean_val) > 0.1_wp .or. abs(std_val - 1.0_wp) > 0.1_wp) then
            print '(A)', "  ✗ Normal distribution parameters incorrect"
            failures = failures + 1
        else
            print '(A,2F8.5)', "  ✓ Normal(0,1): mean=", mean_val, " std=", std_val
        end if
        
        ! Test exponential distribution
        call rng_seed(789_i64)
        do i = 1, size(samples)
            samples(i) = rng_exponential(lambda=2.0_wp)
        end do
        
        mean_val = sum(samples) / size(samples)
        if (abs(mean_val - 0.5_wp) > 0.1_wp) then
            print '(A)', "  ✗ Exponential distribution incorrect"
            failures = failures + 1
        else
            print '(A,F8.5)', "  ✓ Exponential(λ=2): mean≈0.5: ", mean_val
        end if
        
        print *
    end subroutine test_rng
    
    subroutine test_numerics(failures)
        integer, intent(inout) :: failures
        real(wp) :: x(10), y(10), z
        real(wp) :: xp(5), fp(5)
        
        print '(A)', "Testing numerics_utils module..."
        
        ! Test linspace
        x = linspace(0.0_wp, 9.0_wp, 10)
        if (.not. nearly_equal(x(1), 0.0_wp) .or. &
            .not. nearly_equal(x(10), 9.0_wp)) then
            print '(A)', "  ✗ linspace failed"
            failures = failures + 1
        else
            print '(A)', "  ✓ linspace works correctly"
        end if
        
        ! Test clip
        z = clip(5.0_wp, 0.0_wp, 1.0_wp)
        if (.not. nearly_equal(z, 1.0_wp)) then
            print '(A)', "  ✗ clip failed"
            failures = failures + 1
        else
            print '(A)', "  ✓ clip works correctly"
        end if
        
        ! Test interp1d
        xp = [0.0_wp, 1.0_wp, 2.0_wp, 3.0_wp, 4.0_wp]
        fp = [0.0_wp, 1.0_wp, 4.0_wp, 9.0_wp, 16.0_wp]
        z = interp1d(xp, fp, 2.5_wp)
        if (.not. nearly_equal(z, 6.5_wp, 1.0e-6_wp)) then
            print '(A)', "  ✗ interp1d failed"
            failures = failures + 1
        else
            print '(A,F8.5)', "  ✓ Interpolation: ", z
        end if
        
        ! Test trapz
        x = linspace(0.0_wp, PI, 100)
        y = sin(x)
        z = trapz(y, x)
        if (.not. nearly_equal(z, 2.0_wp, 0.01_wp)) then
            print '(A)', "  ✗ trapz failed"
            failures = failures + 1
        else
            print '(A,F8.5)', "  ✓ ∫sin(x)dx from 0 to π ≈ 2: ", z
        end if
        
        ! Test norm2
        x = [3.0_wp, 4.0_wp, 0.0_wp, 0.0_wp, 0.0_wp, &
             0.0_wp, 0.0_wp, 0.0_wp, 0.0_wp, 0.0_wp]
        z = norm2(x(1:2))
        if (.not. nearly_equal(z, 5.0_wp)) then
            print '(A)', "  ✗ norm2 failed"
            failures = failures + 1
        else
            print '(A)', "  ✓ Vector norm works correctly"
        end if
        
        ! Test safe functions
        z = safe_divide(10.0_wp, 0.0_wp)
        if (.not. nearly_equal(z, 0.0_wp)) then
            print '(A)', "  ✗ safe_divide failed"
            failures = failures + 1
        else
            print '(A)', "  ✓ Safe division prevents errors"
        end if
        
        z = safe_sqrt(-1.0_wp)
        if (.not. nearly_equal(z, 0.0_wp)) then
            print '(A)', "  ✗ safe_sqrt failed"
            failures = failures + 1
        else
            print '(A)', "  ✓ Safe square root prevents errors"
        end if
        
        print *
    end subroutine test_numerics

end program test_core