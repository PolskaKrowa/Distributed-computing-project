module mc_estimate_pi
  use kinds
  use constants
  implicit none
contains

  subroutine count_inside_circle(n, count)
    integer(i64), value :: n
    integer(i64), intent(out) :: count

    integer(i64) :: i
    real(r64) :: x, y

    count = 0_i64
    do i = 1, n
      call random_number(x)
      call random_number(y)
      x = two_r64*x - one_r64
      y = two_r64*y - one_r64
      if (x*x + y*y <= one_r64) count = count + 1_i64
    end do
  end subroutine count_inside_circle

end module mc_estimate_pi
