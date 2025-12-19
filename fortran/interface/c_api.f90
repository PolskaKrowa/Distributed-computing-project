module c_api
  use kinds
  use pi_experiment
  implicit none
contains

  subroutine mc_pi(samples, seed, inside) bind(C, name="mc_pi")
    integer(i64), value :: samples
    integer(i32), value :: seed
    integer(i64), intent(out) :: inside

    call run_pi_experiment(samples, seed, inside)
  end subroutine mc_pi

end module c_api
