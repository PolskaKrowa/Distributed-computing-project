! examples/example_damped_oscil.f90
!
! Practical example demonstrating how to use the Fortran core modules
! in a simple numerical simulation.
!
! This example simulates a damped harmonic oscillator with random forcing.
!
! Build:
!   gfortran -o damped_oscil kinds.f90 constants.f90 rng.f90 \
!            numerics_utils.f90 example_damped_oscil.f90
!
program example_damped_oscil
    use kinds
    use constants
    use rng
    use numerics_utils
    implicit none
    
    ! Simulation parameters
    real(wp), parameter :: omega = 2.0_wp * PI      ! Angular frequency (rad/s)
    real(wp), parameter :: gamma = 0.1_wp           ! Damping coefficient
    real(wp), parameter :: noise_strength = 0.5_wp  ! Random forcing strength
    real(wp), parameter :: dt = 0.01_wp             ! Time step (s)
    real(wp), parameter :: t_max = 10.0_wp          ! Total time (s)
    
    ! Arrays
    integer, parameter :: n_steps = int(t_max / dt)
    real(wp) :: time(n_steps)
    real(wp) :: position(n_steps)
    real(wp) :: velocity(n_steps)
    real(wp) :: energy(n_steps)
    
    ! Working variables
    real(wp) :: x, v, a
    real(wp) :: force, kinetic, potential
    integer :: i
    
    print '(A)', "=========================================="
    print '(A)', "Damped Harmonic Oscillator Simulation"
    print '(A)', "=========================================="
    print *
    print '(A,F6.3,A)', "Angular frequency: ", omega, " rad/s"
    print '(A,F6.3)',   "Damping:          ", gamma
    print '(A,F6.3)',   "Noise strength:   ", noise_strength
    print '(A,F6.3,A)', "Time step:        ", dt, " s"
    print '(A,F6.3,A)', "Total time:       ", t_max, " s"
    print *
    
    ! Initialize RNG with deterministic seed
    call rng_seed(42_i64)
    print '(A)', "RNG initialized with seed = 42"
    print *
    
    ! Generate time array
    time = linspace(0.0_wp, t_max, n_steps)
    
    ! Initial conditions
    x = 1.0_wp  ! Initial displacement (m)
    v = 0.0_wp  ! Initial velocity (m/s)
    
    ! Time integration using Euler method with random forcing
    print '(A)', "Running simulation..."
    do i = 1, n_steps
        ! Store current state
        position(i) = x
        velocity(i) = v
        
        ! Calculate energy
        kinetic = 0.5_wp * v**2
        potential = 0.5_wp * omega**2 * x**2
        energy(i) = kinetic + potential
        
        ! Random forcing
        force = rng_normal(mean=0.0_wp, sigma=noise_strength)
        
        ! Acceleration: a = -omega^2 * x - gamma * v + force
        a = -omega**2 * x - gamma * v + force
        
        ! Euler integration
        v = v + a * dt
        x = x + v * dt
    end do
    print '(A)', "Simulation complete!"
    print *
    
    ! Calculate statistics
    call print_statistics()
    
    ! Demonstrate interpolation
    call demonstrate_interpolation()
    
    print *
    print '(A)', "=========================================="
    print '(A)', "Example completed successfully!"
    print '(A)', "=========================================="
    
contains

    subroutine print_statistics()
        real(wp) :: mean_x, std_x, mean_e
        real(wp) :: max_x, min_x
        
        print '(A)', "Statistics:"
        print '(A)', "----------"
        
        ! Position statistics
        mean_x = sum(position) / size(position)
        std_x = sqrt(sum((position - mean_x)**2) / size(position))
        max_x = maxval(position)
        min_x = minval(position)
        
        print '(A,F8.5,A)', "Position mean:    ", mean_x, " m"
        print '(A,F8.5,A)', "Position std:     ", std_x, " m"
        print '(A,F8.5,A)', "Position max:     ", max_x, " m"
        print '(A,F8.5,A)', "Position min:     ", min_x, " m"
        print *
        
        ! Energy statistics
        mean_e = sum(energy) / size(energy)
        print '(A,F8.5,A)', "Mean energy:      ", mean_e, " J"
        print '(A,F8.5,A)', "Initial energy:   ", energy(1), " J"
        print '(A,F8.5,A)', "Final energy:     ", energy(n_steps), " J"
        print *
        
        ! Verify energy dissipation due to damping
        if (energy(n_steps) < energy(1)) then
            print '(A)', "✓ Energy dissipated as expected (damping)"
        else
            print '(A)', "✗ Warning: Energy increased (check simulation)"
        end if
        print *
    end subroutine print_statistics
    
    subroutine demonstrate_interpolation()
        real(wp) :: t_interp, x_interp
        integer :: idx
        
        print '(A)', "Interpolation example:"
        print '(A)', "---------------------"
        
        ! Interpolate position at t = 2.5 s
        t_interp = 2.5_wp
        x_interp = interp1d(time, position, t_interp)
        
        print '(A,F6.3,A)', "Position at t = ", t_interp, " s:"
        print '(A,F8.5,A)', "  Interpolated:  ", x_interp, " m"
        
        ! Compare with nearest actual value
        idx = minloc(abs(time - t_interp), 1)
        print '(A,F8.5,A)', "  Nearest point: ", position(idx), " m"
        print '(A,F6.3,A)', "  (at t = ", time(idx), " s)"
        print *
    end subroutine demonstrate_interpolation

end program example_damped_oscil