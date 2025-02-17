// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#pragma once

#include <units/current.h>
#include <units/impedance.h>
#include <units/power.h>
#include <units/voltage.h>
#include <wpi/circular_buffer.h>

namespace frc {

/**
 * Finds the resistance of a channel or the entire robot using a running linear
 * regression over a window. Must be updated with current and voltage
 * periodically using the <code>Calculate</code> method. <p>To use this for
 * finding the resistance of a channel, use the calculate method with the
 * battery voltage minus the voltage at the motor controller or whatever is
 * plugged in to the PDP at that channel.</p>
 */
class ResistanceCalculator {
 public:
  static constexpr int kDefaultBufferSize = 250;
  static constexpr double kDefaultRSquaredThreshold = 0.75;

  /**
   * Create a ResistanceCalculator to find the resistance of a channel using a
   * running linear regression over a window. Must be updated with current and
   * voltage periodically using the <code>Calculate</code> method.
   *
   * @param bufferSize The maximum number of points to take the linear
   * regression over.
   * @param rSquaredThreshold The minimum R² value (0 to 1) considered
   * significant enough to return the regression slope instead of NaN. A lower
   * threshold allows resistance to be returned even with noisier data.
   */
  ResistanceCalculator(int bufferSize, double rSquaredThreshold);

  /**
   * Create a ResistanceCalculator to find the resistance of a channel using a
   * running linear regression over a window. Must be updated with current and
   * voltage periodically using the <code>Calculate</code> method. <p>Uses a
   * buffer size of 250 and an R² threshold of 0.5.</p>
   */
  ResistanceCalculator();

  ~ResistanceCalculator() = default;
  ResistanceCalculator(ResistanceCalculator&&) = default;
  ResistanceCalculator& operator=(ResistanceCalculator&&) = default;

  /**
   * Update the buffers with new (current, voltage) points, and remove old
   * points if necessary.
   *
   * @param current The current current
   * @param voltage The current voltage
   * @return The current resistance in ohms
   */
  units::ohm_t Calculate(units::ampere_t current, units::volt_t voltage);

 private:
  using ampere_squared_t = units::unit_t<units::squared<units::amperes>>;
  using volt_squared_t = units::unit_t<units::squared<units::volts>>;

  class OnlineCovariance {
   private:
    /** Number of points covariance is calculated over. */
    int m_n;

    /** Current mean of x values. */
    double m_xMean;

    /** Current mean of y values. */
    double m_yMean;

    /** Current approximated population covariance. */
    double m_cov;

   public:
    OnlineCovariance() = default;
    ~OnlineCovariance() = default;
    OnlineCovariance(OnlineCovariance&&) = default;
    OnlineCovariance& operator=(OnlineCovariance&&) = default;

    /** The previously calculated covariance. */
    double GetCovariance();

    /**
     * Calculate the covariance based on a new point that may be removed or
     * added.
     * @param x The x value of the point.
     * @param y The y value of the point.
     * @param remove Whether to remove the point or add it.
     * @return The new sample covariance.
     */
    double Calculate(double x, double y, bool remove);
  };

  /**
   * Buffers holding the current values that will eventually need to be
   * subtracted from the sum when they leave the window.
   */
  wpi::circular_buffer<double> m_currentBuffer;

  /**
   * Buffer holding the voltage values that will eventually need to be
   * subtracted from the sum when they leave the window.
   */
  wpi::circular_buffer<double> m_voltageBuffer;

  /**
   * The maximum number of points to take the linear regression over.
   */
  int m_bufferSize;

  /**
   * The minimum R² value considered significant enough to return the
   * regression slope instead of NaN.
   */
  double m_rSquaredThreshold;

  /** Used for approximating current variance. */
  OnlineCovariance m_currentVariance;

  /** Used for approximating voltage variance. */
  OnlineCovariance m_voltageVariance;

  /** Used for approximating covariance of current and voltage. */
  OnlineCovariance m_covariance;

  /**
   * The number of points currently in the buffer.
   */
  int m_numPoints;
};

}  // namespace frc
