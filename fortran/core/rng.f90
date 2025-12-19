module rng_core
  use kinds
  use constants
  implicit none
contains

  subroutine seed_rng(seed)
    integer(i32), value :: seed
    integer :: n, i
    integer, allocatable :: seed_array(:)

    call random_seed(size = n)
    allocate(seed_array(n))
    do i = 1, n
      seed_array(i) = mod(int(seed) + i*1664525, 2147483647)
    end do
    call random_seed(put = seed_array)
    deallocate(seed_array)
  end subroutine seed_rng

end module rng_core