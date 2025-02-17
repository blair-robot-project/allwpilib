// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#pragma once

#include <wpi/SymbolExports.h>

#include "Quaternion.h"
#include "Rotation2d.h"
#include "frc/EigenCore.h"
#include "units/angle.h"

namespace frc {

/**
 * A rotation in a 3D coordinate frame.
 */
class WPILIB_DLLEXPORT Rotation3d {
 public:
  /**
   * Constructs a Rotation3d with a default angle of 0 degrees.
   */
  Rotation3d() = default;

  /**
   * Constructs a Rotation3d from a quaternion.
   *
   * @param q The quaternion.
   */
  explicit Rotation3d(const Quaternion& q);

  /**
   * Constructs a Rotation3d from extrinsic roll, pitch, and yaw.
   *
   * Extrinsic rotations occur in that order around the axes in the fixed global
   * frame rather than the body frame.
   *
   * @param roll The counterclockwise rotation angle around the X axis (roll).
   * @param pitch The counterclockwise rotation angle around the Y axis (pitch).
   * @param yaw The counterclockwise rotation angle around the Z axis (yaw).
   */
  Rotation3d(units::radian_t roll, units::radian_t pitch, units::radian_t yaw);

  /**
   * Constructs a Rotation3d with the given axis-angle representation. The axis
   * doesn't have to be normalized.
   *
   * @param axis The rotation axis.
   * @param angle The rotation around the axis.
   */
  Rotation3d(const Vectord<3>& axis, units::radian_t angle);

  /**
   * Adds two rotations together.
   *
   * @param other The rotation to add.
   *
   * @return The sum of the two rotations.
   */
  Rotation3d operator+(const Rotation3d& other) const;

  /**
   * Subtracts the new rotation from the current rotation and returns the new
   * rotation.
   *
   * @param other The rotation to subtract.
   *
   * @return The difference between the two rotations.
   */
  Rotation3d operator-(const Rotation3d& other) const;

  /**
   * Takes the inverse of the current rotation.
   *
   * @return The inverse of the current rotation.
   */
  Rotation3d operator-() const;

  /**
   * Multiplies the current rotation by a scalar.
   * @param scalar The scalar.
   *
   * @return The new scaled Rotation3d.
   */
  Rotation3d operator*(double scalar) const;

  /**
   * Checks equality between this Rotation3d and another object.
   *
   * @param other The other object.
   * @return Whether the two objects are equal.
   */
  bool operator==(const Rotation3d& other) const;

  /**
   * Checks inequality between this Rotation3d and another object.
   *
   * @param other The other object.
   * @return Whether the two objects are not equal.
   */
  bool operator!=(const Rotation3d& other) const;

  /**
   * Adds the new rotation to the current rotation.
   *
   * @param other The rotation to rotate by.
   *
   * @return The new rotated Rotation3d.
   */
  Rotation3d RotateBy(const Rotation3d& other) const;

  /**
   * Returns the quaternion representation of the Rotation3d.
   */
  const Quaternion& GetQuaternion() const;

  /**
   * Returns the counterclockwise rotation angle around the X axis (roll).
   */
  units::radian_t X() const;

  /**
   * Returns the counterclockwise rotation angle around the Y axis (pitch).
   */
  units::radian_t Y() const;

  /**
   * Returns the counterclockwise rotation angle around the Z axis (yaw).
   */
  units::radian_t Z() const;

  /**
   * Returns the axis in the axis-angle representation of this rotation.
   */
  Vectord<3> Axis() const;

  /**
   * Returns the angle in the axis-angle representation of this rotation.
   */
  units::radian_t Angle() const;

  /**
   * Returns a Rotation2d representing this Rotation3d projected into the X-Y
   * plane.
   */
  Rotation2d ToRotation2d() const;

 private:
  Quaternion m_q;
};

}  // namespace frc
