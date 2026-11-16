#pragma once

#include <stdexcept>
#include <string>

namespace optinspect {

/// Base exception type for all OptiInspect domain errors.
/// Using a dedicated hierarchy (instead of raw std::runtime_error everywhere)
/// lets calling code distinguish inspection-engine failures from generic
/// standard-library exceptions and from third-party (OpenCV) exceptions.
class OptInspectError : public std::runtime_error {
public:
    explicit OptInspectError(const std::string& message) : std::runtime_error(message) {}
};

/// Thrown when an image cannot be located, read, or decoded.
class ImageLoadError : public OptInspectError {
public:
    explicit ImageLoadError(const std::string& message) : OptInspectError(message) {}
};

/// Thrown when a configuration file is missing, malformed, or contains
/// invalid parameter values.
class ConfigurationError : public OptInspectError {
public:
    explicit ConfigurationError(const std::string& message) : OptInspectError(message) {}
};

/// Thrown when image alignment cannot produce a reliable transform.
/// This is caught by the pipeline and converted into a REVIEW decision
/// rather than allowed to propagate as a fatal error.
class AlignmentError : public OptInspectError {
public:
    explicit AlignmentError(const std::string& message) : OptInspectError(message) {}
};

}  // namespace optinspect
