module kinds
  use iso_c_binding
  implicit none
  private

  public :: i32, i64, r64

  integer, parameter :: i32 = c_int32_t
  integer, parameter :: i64 = c_int64_t
  integer, parameter :: r64 = c_double

end module kinds