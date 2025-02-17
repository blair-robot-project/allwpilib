// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#pragma once

#include <wpi/SymbolExports.h>
#include <wpi/array.h>

#include "frc/EigenCore.h"
#include "frc/system/LinearSystem.h"
#include "units/time.h"

namespace frc {

/**
 * A Kalman filter combines predictions from a model and measurements to give an
 * estimate of the true system state. This is useful because many states cannot
 * be measured directly as a result of sensor noise, or because the state is
 * "hidden".
 *
 * Kalman filters use a K gain matrix to determine whether to trust the model or
 * measurements more. Kalman filter theory uses statistics to compute an optimal
 * K gain which minimizes the sum of squares error in the state estimate. This K
 * gain is used to correct the state estimate by some amount of the difference
 * between the actual measurements and the measurements predicted by the model.
 *
 * For more on the underlying math, read
 * https://file.tavsys.net/control/controls-engineering-in-frc.pdf chapter 9
 * "Stochastic control theory".
 *
 * @tparam States The number of states.
 * @tparam Inputs The number of inputs.
 * @tparam Outputs The number of outputs.
 */
template <int States, int Inputs, int Outputs>
class KalmanFilter {
 public:
  using StateVector = Vectord<States>;
  using InputVector = Vectord<Inputs>;
  using OutputVector = Vectord<Outputs>;

  using StateArray = wpi::array<double, States>;
  using OutputArray = wpi::array<double, Outputs>;

  /**
   * Constructs a state-space observer with the given plant.
   *
   * @param plant              The plant used for the prediction step.
   * @param stateStdDevs       Standard deviations of model states.
   * @param measurementStdDevs Standard deviations of measurements.
   * @param dt                 Nominal discretization timestep.
   */
  KalmanFilter(LinearSystem<States, Inputs, Outputs>& plant,
               const StateArray& stateStdDevs,
               const OutputArray& measurementStdDevs, units::second_t dt);

  KalmanFilter(KalmanFilter&&) = default;
  KalmanFilter& operator=(KalmanFilter&&) = default;

  /**
   * Returns the steady-state Kalman gain matrix K.
   */
  const Matrixd<States, Outputs>& K() const { return m_K; }

  /**
   * Returns an element of the steady-state Kalman gain matrix K.
   *
   * @param i Row of K.
   * @param j Column of K.
   */
  double K(int i, int j) const { return m_K(i, j); }

  /**
   * Returns the state estimate x-hat.
   */
  const StateVector& Xhat() const { return m_xHat; }

  /**
   * Returns an element of the state estimate x-hat.
   *
   * @param i Row of x-hat.
   */
  double Xhat(int i) const { return m_xHat(i); }

  /**
   * Set initial state estimate x-hat.
   *
   * @param xHat The state estimate x-hat.
   */
  void SetXhat(const StateVector& xHat) { m_xHat = xHat; }

  /**
   * Set an element of the initial state estimate x-hat.
   *
   * @param i     Row of x-hat.
   * @param value Value for element of x-hat.
   */
  void SetXhat(int i, double value) { m_xHat(i) = value; }

  /**
   * Resets the observer.
   */
  void Reset() { m_xHat.setZero(); }

  /**
   * Project the model into the future with a new control input u.
   *
   * @param u  New control input from controller.
   * @param dt Timestep for prediction.
   */
  void Predict(const InputVector& u, units::second_t dt);

  /**
   * Correct the state estimate x-hat using the measurements in y.
   *
   * @param u Same control input used in the last predict step.
   * @param y Measurement vector.
   */
  void Correct(const InputVector& u, const OutputVector& y);

 private:
  LinearSystem<States, Inputs, Outputs>* m_plant;

  /**
   * The steady-state Kalman gain matrix.
   */
  Matrixd<States, Outputs> m_K;

  /**
   * The state estimate.
   */
  StateVector m_xHat;
};

extern template class EXPORT_TEMPLATE_DECLARE(WPILIB_DLLEXPORT)
    KalmanFilter<1, 1, 1>;
extern template class EXPORT_TEMPLATE_DECLARE(WPILIB_DLLEXPORT)
    KalmanFilter<2, 1, 1>;

}  // namespace frc

#include "KalmanFilter.inc"
