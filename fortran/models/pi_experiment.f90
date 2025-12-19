module pi_experiment
  use kinds
  use rng_core
  use mc_estimate_pi
  implicit none
contains

  subroutine run_pi_experiment(samples, seed, inside)
    integer(i64), value :: samples
    integer(i32), value :: seed
    integer(i64), intent(out) :: inside

    call seed_rng(seed)
    call count_inside_circle(samples, inside)
  end subroutine run_pi_experiment

end module pi_experiment
