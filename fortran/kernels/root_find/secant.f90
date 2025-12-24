! fortran/kernels/root_find/secant.f90
!
! Secant method for finding roots of nonlinear equations.
! Uses only function evaluations (no derivatives required).
! Converges superlinearly with order φ ≈ 1.618 (golden ratio).
!
! The method iterates: x_{n+1} = x_n - f(x_n) * (x_n - x_{n-1}) / (f(x_n) - f(x_{n-1}))
!
! Usage:
!   use secant
!   real(wp) :: root
!   integer :: status
!   call secant_solve(f, x0, x1, root, status)
!
module secant
    use kinds, only: wp, i32
    use constants, only: TOL_DEFAULT
    implicit none
    private
    
    ! Public interface
    public :: secant_solve
    public :: secant_solve_bracket
    public :: secant_params_t
    
    ! Abstract interface for user function
    abstract interface
        pure function func_1d(x) result(f)
            import :: wp
            real(wp), intent(in) :: x
            real(wp) :: f
        end function func_1d
    end interface
    
    ! Configuration parameters
    type :: secant_params_t
        real(wp) :: tol = 1.0e-10_wp       ! Convergence tolerance
        real(wp) :: tol_f = 1.0e-10_wp     ! Function value tolerance
        integer :: max_iter = 100           ! Maximum iterations
        real(wp) :: step_limit = 1.0e10_wp  ! Maximum step size
    end type secant_params_t
    
    ! Status codes
    integer, parameter, public :: SEC_SUCCESS = 0
    integer, parameter, public :: SEC_MAX_ITER = 1
    integer, parameter, public :: SEC_DIVERGED = 2
    integer, parameter, public :: SEC_FLAT_FUNCTION = 3
    integer, parameter, public :: SEC_INVALID_INPUT = 4
    
contains

    !> Solve f(x) = 0 using Secant method
    !!
    !! @param[in] f Function to find root of
    !! @param[in] x0 First initial point
    !! @param[in] x1 Second initial point (should be close to x0)
    !! @param[out] root Computed root (if successful)
    !! @param[out] status Return status code
    !! @param[in] params Optional parameters (uses defaults if not provided)
    !! @param[out] num_iter Number of iterations performed
    subroutine secant_solve(f, x0, x1, root, status, params, num_iter)
        procedure(func_1d) :: f
        real(wp), intent(in) :: x0, x1
        real(wp), intent(out) :: root
        integer, intent(out) :: status
        type(secant_params_t), intent(in), optional :: params
        integer, intent(out), optional :: num_iter
        
        type(secant_params_t) :: p
        real(wp) :: x_prev, x_curr, x_next
        real(wp) :: f_prev, f_curr
        real(wp) :: dx, df
        integer :: iter
        
        ! Set parameters
        if (present(params)) then
            p = params
        else
            p = secant_params_t()
        end if
        
        ! Validate input
        if (.not. is_finite(x0) .or. .not. is_finite(x1)) then
            status = SEC_INVALID_INPUT
            root = x0
            if (present(num_iter)) num_iter = 0
            return
        end if
        
        ! Initialise
        x_prev = x0
        x_curr = x1
        f_prev = f(x_prev)
        f_curr = f(x_curr)
        
        ! Check if we're already at a root
        if (abs(f_curr) < p%tol_f) then
            status = SEC_SUCCESS
            root = x_curr
            if (present(num_iter)) num_iter = 0
            return
        end if
        
        status = SEC_MAX_ITER
        
        ! Main iteration
        do iter = 1, p%max_iter
            ! Check for flat function (denominator too small)
            df = f_curr - f_prev
            if (abs(df) < tiny(1.0_wp)) then
                status = SEC_FLAT_FUNCTION
                root = x_curr
                if (present(num_iter)) num_iter = iter
                return
            end if
            
            ! Compute secant step
            dx = -f_curr * (x_curr - x_prev) / df
            
            ! Limit step size to prevent wild jumps
            if (abs(dx) > p%step_limit) then
                dx = sign(p%step_limit, dx)
            end if
            
            ! Update points
            x_next = x_curr + dx
            
            ! Check for convergence
            if (abs(dx) < p%tol * (1.0_wp + abs(x_curr))) then
                ! Verify convergence with function value
                if (abs(f_curr) < p%tol_f) then
                    status = SEC_SUCCESS
                    root = x_next
                    if (present(num_iter)) num_iter = iter
                    return
                end if
            end if
            
            ! Check for divergence
            if (.not. is_finite(x_next)) then
                status = SEC_DIVERGED
                root = x_curr
                if (present(num_iter)) num_iter = iter
                return
            end if
            
            ! Shift points for next iteration
            x_prev = x_curr
            f_prev = f_curr
            x_curr = x_next
            f_curr = f(x_curr)
            
            ! Check convergence on function value
            if (abs(f_curr) < p%tol_f) then
                status = SEC_SUCCESS
                root = x_curr
                if (present(num_iter)) num_iter = iter
                return
            end if
        end do
        
        ! Max iterations reached
        root = x_curr
        if (present(num_iter)) num_iter = p%max_iter
        
    end subroutine secant_solve
    
    !> Secant method with bracketing (Regula Falsi variant)
    !!
    !! Uses the Illinois algorithm to prevent stalling.
    !! Guarantees convergence if f(a) and f(b) have opposite signs.
    !!
    !! @param[in] f Function to find root of
    !! @param[in] a Lower bracket
    !! @param[in] b Upper bracket (f(a) and f(b) must have opposite signs)
    !! @param[out] root Computed root (if successful)
    !! @param[out] status Return status code
    !! @param[in] params Optional parameters
    !! @param[out] num_iter Number of iterations performed
    subroutine secant_solve_bracket(f, a, b, root, status, params, num_iter)
        procedure(func_1d) :: f
        real(wp), intent(in) :: a, b
        real(wp), intent(out) :: root
        integer, intent(out) :: status
        type(secant_params_t), intent(in), optional :: params
        integer, intent(out), optional :: num_iter
        
        type(secant_params_t) :: p
        real(wp) :: x_low, x_high, f_low, f_high
        real(wp) :: x_new, f_new
        integer :: iter, side
        real(wp) :: delta
        
        ! Set parameters
        if (present(params)) then
            p = params
        else
            p = secant_params_t()
        end if
        
        ! Validate bracket
        f_low = f(a)
        f_high = f(b)
        
        if (.not. is_finite(a) .or. .not. is_finite(b) .or. &
            .not. is_finite(f_low) .or. .not. is_finite(f_high)) then
            status = SEC_INVALID_INPUT
            root = 0.5_wp * (a + b)
            if (present(num_iter)) num_iter = 0
            return
        end if
        
        if (f_low * f_high > 0.0_wp) then
            status = SEC_INVALID_INPUT
            root = 0.5_wp * (a + b)
            if (present(num_iter)) num_iter = 0
            return
        end if
        
        ! Set up bracket (ensure x_low < x_high)
        if (a < b) then
            x_low = a
            x_high = b
        else
            x_low = b
            x_high = a
            f_low = f_high
            f_high = f(x_low)
        end if
        
        ! Check if we're at a root already
        if (abs(f_low) < p%tol_f) then
            status = SEC_SUCCESS
            root = x_low
            if (present(num_iter)) num_iter = 0
            return
        end if
        if (abs(f_high) < p%tol_f) then
            status = SEC_SUCCESS
            root = x_high
            if (present(num_iter)) num_iter = 0
            return
        end if
        
        status = SEC_MAX_ITER
        side = 0  ! Track which side was used last (for Illinois algorithm)
        
        ! Main iteration
        do iter = 1, p%max_iter
            ! Check bracket width
            delta = x_high - x_low
            if (delta < p%tol * (1.0_wp + abs(x_low))) then
                status = SEC_SUCCESS
                root = 0.5_wp * (x_low + x_high)
                if (present(num_iter)) num_iter = iter
                return
            end if
            
            ! Compute secant point (Regula Falsi)
            if (abs(f_high - f_low) < tiny(1.0_wp)) then
                ! Fall back to bisection if denominator too small
                x_new = 0.5_wp * (x_low + x_high)
            else
                x_new = x_low - f_low * (x_high - x_low) / (f_high - f_low)
                
                ! Ensure x_new is within bracket (should be by construction)
                x_new = max(x_low, min(x_high, x_new))
            end if
            
            f_new = f(x_new)
            
            ! Check convergence
            if (abs(f_new) < p%tol_f) then
                status = SEC_SUCCESS
                root = x_new
                if (present(num_iter)) num_iter = iter
                return
            end if
            
            ! Update bracket
            if (f_new * f_low < 0.0_wp) then
                ! Root is between x_low and x_new
                x_high = x_new
                f_high = f_new
                
                ! Illinois algorithm: if same side chosen twice, halve the other side
                if (side == -1) then
                    f_low = f_low * 0.5_wp
                end if
                side = -1
            else
                ! Root is between x_new and x_high
                x_low = x_new
                f_low = f_new
                
                ! Illinois algorithm
                if (side == 1) then
                    f_high = f_high * 0.5_wp
                end if
                side = 1
            end if
        end do
        
        ! Max iterations reached
        root = 0.5_wp * (x_low + x_high)
        if (present(num_iter)) num_iter = p%max_iter
        
    end subroutine secant_solve_bracket
    
    !> Check if value is finite (helper function)
    pure function is_finite(x) result(finite)
        real(wp), intent(in) :: x
        logical :: finite
        
        finite = .not. (x /= x) .and. (abs(x) <= huge(1.0_wp))
    end function is_finite

end module secant