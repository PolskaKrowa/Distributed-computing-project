module constants
  use kinds
  implicit none
  private

  public :: zero_r64, one_r64, two_r64

  real(r64), parameter :: zero_r64 = 0.0_r64
  real(r64), parameter :: one_r64  = 1.0_r64
  real(r64), parameter :: two_r64  = 2.0_r64

end module constants