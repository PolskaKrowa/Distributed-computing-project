! fortran/kernels/root_find/newton_raphson.f90
!
! Newton-Raphson method for finding roots of nonlinear equations.
! Uses both function and derivative evaluations for quadratic convergence.
!
! The method iterates: x_{n+1} = x_n - f(x_n)/f'(x_n)
!
! Usage:
!   use newton_raphson
!   real(wp) :: root
!   integer :: status
!   call newton_raphson_solve(f, df, x0, root, status)
!
module newton_raphson
    use kinds, only: wp, i32
    use constants, only: TOL_DEFAULT
    implicit none
    private
    
    ! Public interface
    public :: newton_raphson_solve
    public :: newton_raphson_solve_bracket
    public :: newton_raphson_params_t
    
    ! Abstract interface for user function
    abstract interface
        pure function func_1d(x) result(f)
            import :: wp
            real(wp), intent(in) :: x
            real(wp) :: f
        end function func_1d
    end interface
    
    ! Configuration parameters
    type :: newton_raphson_params_t
        real(wp) :: tol = 1.0e-10_wp      ! Convergence tolerance
        real(wp) :: tol_f = 1.0e-10_wp    ! Function value tolerance
        integer :: max_iter = 100          ! Maximum iterations
        real(wp) :: step_limit = 1.0e10_wp ! Maximum step size
        logical :: use_bracketing = .false. ! Keep within initial bracket
    end type newton_raphson_params_t
    
    ! Status codes
    integer, parameter, public :: NR_SUCCESS = 0
    integer, parameter, public :: NR_MAX_ITER = 1
    integer, parameter, public :: NR_DIVERGED = 2
    integer, parameter, public :: NR_ZERO_DERIVATIVE = 3
    integer, parameter, public :: NR_INVALID_INPUT = 4
    
contains

    !> Solve f(x) = 0 using Newton-Raphson method
    !!
    !! @param[in] f Function to find root of
    !! @param[in] df Derivative of function
    !! @param[in] x0 Initial guess
    !! @param[out] root Computed root (if successful)
    !! @param[out] status Return status code
    !! @param[in] params Optional parameters (uses defaults if not provided)
    !! @param[out] num_iter Number of iterations performed
    subroutine newton_raphson_solve(f, df, x0, root, status, params, num_iter)
        procedure(func_1d) :: f, df
        real(wp), intent(in) :: x0
        real(wp), intent(out) :: root
        integer, intent(out) :: status
        type(newton_raphson_params_t), intent(in), optional :: params
        integer, intent(out), optional :: num_iter
        
        type(newton_raphson_params_t) :: p
        real(wp) :: x, x_new, fx, dfx, dx
        integer :: iter
        
        ! Set parameters
        if (present(params)) then
            p = params
        else
            p = newton_raphson_params_t()
        end if
        
        ! Validate input
        if (.not. is_finite(x0)) then
            status = NR_INVALID_INPUT
            root = x0
            if (present(num_iter)) num_iter = 0
            return
        end if
        
        ! Initialise
        x = x0
        status = NR_MAX_ITER
        
        ! Main iteration
        do iter = 1, p%max_iter
            ! Evaluate function and derivative
            fx = f(x)
            dfx = df(x)
            
            ! Check for convergence on function value
            if (abs(fx) < p%tol_f) then
                status = NR_SUCCESS
                root = x
                if (present(num_iter)) num_iter = iter
                return
            end if
            
            ! Check for zero derivative
            if (abs(dfx) < tiny(1.0_wp)) then
                status = NR_ZERO_DERIVATIVE
                root = x
                if (present(num_iter)) num_iter = iter
                return
            end if
            
            ! Compute Newton step
            dx = -fx / dfx
            
            ! Limit step size to prevent wild jumps
            if (abs(dx) > p%step_limit) then
                dx = sign(p%step_limit, dx)
            end if
            
            ! Update
            x_new = x + dx
            
            ! Check for convergence on x
            if (abs(dx) < p%tol * (1.0_wp + abs(x))) then
                status = NR_SUCCESS
                root = x_new
                if (present(num_iter)) num_iter = iter
                return
            end if
            
            ! Check for divergence
            if (.not. is_finite(x_new)) then
                status = NR_DIVERGED
                root = x
                if (present(num_iter)) num_iter = iter
                return
            end if
            
            x = x_new
        end do
        
        ! Max iterations reached
        root = x
        if (present(num_iter)) num_iter = p%max_iter
        
    end subroutine newton_raphson_solve
    
    !> Newton-Raphson with bracketing to ensure convergence
    !!
    !! Keeps the root estimate within the initial bracket [a, b].
    !! Falls back to bisection if Newton step goes outside bracket.
    !!
    !! @param[in] f Function to find root of
    !! @param[in] df Derivative of function
    !! @param[in] a Lower bracket (f(a) and f(b) must have opposite signs)
    !! @param[in] b Upper bracket
    !! @param[out] root Computed root (if successful)
    !! @param[out] status Return status code
    !! @param[in] params Optional parameters
    !! @param[out] num_iter Number of iterations performed
    subroutine newton_raphson_solve_bracket(f, df, a, b, root, status, params, num_iter)
        procedure(func_1d) :: f, df
        real(wp), intent(in) :: a, b
        real(wp), intent(out) :: root
        integer, intent(out) :: status
        type(newton_raphson_params_t), intent(in), optional :: params
        integer, intent(out), optional :: num_iter
        
        type(newton_raphson_params_t) :: p
        real(wp) :: x, x_new, fx, dfx, dx
        real(wp) :: x_low, x_high, f_low, f_high
        integer :: iter
        logical :: used_bisection
        
        ! Set parameters
        if (present(params)) then
            p = params
        else
            p = newton_raphson_params_t()
        end if
        
        ! Validate bracket
        f_low = f(a)
        f_high = f(b)
        
        if (.not. is_finite(a) .or. .not. is_finite(b) .or. &
            .not. is_finite(f_low) .or. .not. is_finite(f_high)) then
            status = NR_INVALID_INPUT
            root = 0.5_wp * (a + b)
            if (present(num_iter)) num_iter = 0
            return
        end if
        
        if (f_low * f_high > 0.0_wp) then
            status = NR_INVALID_INPUT
            root = 0.5_wp * (a + b)
            if (present(num_iter)) num_iter = 0
            return
        end if
        
        ! Set up bracket
        if (a < b) then
            x_low = a
            x_high = b
        else
            x_low = b
            x_high = a
        end if
        
        ! Initial guess at midpoint
        x = 0.5_wp * (x_low + x_high)
        status = NR_MAX_ITER
        
        ! Main iteration
        do iter = 1, p%max_iter
            fx = f(x)
            
            ! Check convergence
            if (abs(fx) < p%tol_f .or. (x_high - x_low) < p%tol * (1.0_wp + abs(x))) then
                status = NR_SUCCESS
                root = x
                if (present(num_iter)) num_iter = iter
                return
            end if
            
            ! Try Newton step
            dfx = df(x)
            used_bisection = .false.
            
            if (abs(dfx) > tiny(1.0_wp)) then
                dx = -fx / dfx
                x_new = x + dx
                
                ! Check if Newton step stays in bracket
                if (x_new < x_low .or. x_new > x_high) then
                    used_bisection = .true.
                end if
            else
                used_bisection = .true.
            end if
            
            ! Fall back to bisection if needed
            if (used_bisection) then
                x_new = 0.5_wp * (x_low + x_high)
            end if
            
            ! Update bracket
            if (f(x_new) * f(x_low) < 0.0_wp) then
                x_high = x_new
            else
                x_low = x_new
            end if
            
            x = x_new
        end do
        
        ! Max iterations reached
        root = x
        if (present(num_iter)) num_iter = p%max_iter
        
    end subroutine newton_raphson_solve_bracket
    
    !> Check if value is finite (helper function)
    pure function is_finite(x) result(finite)
        real(wp), intent(in) :: x
        logical :: finite
        
        finite = .not. (x /= x) .and. (abs(x) <= huge(1.0_wp))
    end function is_finite

end module newton_raphson