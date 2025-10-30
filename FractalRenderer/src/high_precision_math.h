// high_precision_math.h
// High-precision arithmetic wrapper for GMP/MPFR
// Enables zoom levels beyond 1e-14

#pragma once

#pragma warning(push)
#pragma warning(disable:4146)
#include <gmp.h>
#pragma warning(pop)
#include <mpfr.h>
#include <complex>
#include <cmath>

// ============================================================================
// High-Precision Float Wrapper
// ============================================================================

class HighPrecisionFloat {
public:
    // Constructors - use explicit types to avoid ambiguity with mpfr_prec_t
    explicit HighPrecisionFloat(mpfr_prec_t precision_bits = 128) {
        mpfr_init2(value, precision_bits);
        mpfr_set_d(value, 0.0, MPFR_RNDN);
    }

    HighPrecisionFloat(double d, mpfr_prec_t precision_bits = 128) {
        mpfr_init2(value, precision_bits);
        mpfr_set_d(value, d, MPFR_RNDN);
    }

    HighPrecisionFloat(const char* str, mpfr_prec_t precision_bits = 128) {
        mpfr_init2(value, precision_bits);
        mpfr_set_str(value, str, 10, MPFR_RNDN);
    }

    // Destructor
    ~HighPrecisionFloat() {
        mpfr_clear(value);
    }

    // Copy constructor
    HighPrecisionFloat(const HighPrecisionFloat& other) {
        mpfr_init2(value, mpfr_get_prec(other.value));
        mpfr_set(value, other.value, MPFR_RNDN);
    }

    // Move constructor
    HighPrecisionFloat(HighPrecisionFloat&& other) noexcept {
        mpfr_init2(value, mpfr_get_prec(other.value));
        mpfr_swap(value, other.value);
    }

    // Assignment operator
    HighPrecisionFloat& operator=(const HighPrecisionFloat& other) {
        if (this != &other) {
            mpfr_set_prec(value, mpfr_get_prec(other.value));
            mpfr_set(value, other.value, MPFR_RNDN);
        }
        return *this;
    }

    // Move assignment
    HighPrecisionFloat& operator=(HighPrecisionFloat&& other) noexcept {
        if (this != &other) {
            mpfr_swap(value, other.value);
        }
        return *this;
    }

    // Conversions
    double to_double() const {
        return mpfr_get_d(value, MPFR_RNDN);
    }

    // Arithmetic operations
    HighPrecisionFloat operator+(const HighPrecisionFloat& other) const {
        HighPrecisionFloat result(mpfr_get_prec(value));
        mpfr_add(result.value, value, other.value, MPFR_RNDN);
        return result;
    }

    HighPrecisionFloat operator-(const HighPrecisionFloat& other) const {
        HighPrecisionFloat result(mpfr_get_prec(value));
        mpfr_sub(result.value, value, other.value, MPFR_RNDN);
        return result;
    }

    HighPrecisionFloat operator*(const HighPrecisionFloat& other) const {
        HighPrecisionFloat result(mpfr_get_prec(value));
        mpfr_mul(result.value, value, other.value, MPFR_RNDN);
        return result;
    }

    HighPrecisionFloat operator/(const HighPrecisionFloat& other) const {
        HighPrecisionFloat result(mpfr_get_prec(value));
        mpfr_div(result.value, value, other.value, MPFR_RNDN);
        return result;
    }

    HighPrecisionFloat& operator+=(const HighPrecisionFloat& other) {
        mpfr_add(value, value, other.value, MPFR_RNDN);
        return *this;
    }

    HighPrecisionFloat& operator-=(const HighPrecisionFloat& other) {
        mpfr_sub(value, value, other.value, MPFR_RNDN);
        return *this;
    }

    HighPrecisionFloat& operator*=(const HighPrecisionFloat& other) {
        mpfr_mul(value, value, other.value, MPFR_RNDN);
        return *this;
    }

    HighPrecisionFloat& operator/=(const HighPrecisionFloat& other) {
        mpfr_div(value, value, other.value, MPFR_RNDN);
        return *this;
    }

    // Comparison operators
    bool operator>(const HighPrecisionFloat& other) const {
        return mpfr_cmp(value, other.value) > 0;
    }

    bool operator<(const HighPrecisionFloat& other) const {
        return mpfr_cmp(value, other.value) < 0;
    }

    bool operator>=(const HighPrecisionFloat& other) const {
        return mpfr_cmp(value, other.value) >= 0;
    }

    bool operator<=(const HighPrecisionFloat& other) const {
        return mpfr_cmp(value, other.value) <= 0;
    }

    bool operator==(const HighPrecisionFloat& other) const {
        return mpfr_cmp(value, other.value) == 0;
    }

    bool operator!=(const HighPrecisionFloat& other) const {
        return mpfr_cmp(value, other.value) != 0;
    }

    // Comparison with double
    bool operator>(double d) const {
        return mpfr_cmp_d(value, d) > 0;
    }

    bool operator<(double d) const {
        return mpfr_cmp_d(value, d) < 0;
    }

    bool operator>=(double d) const {
        return mpfr_cmp_d(value, d) >= 0;
    }

    bool operator<=(double d) const {
        return mpfr_cmp_d(value, d) <= 0;
    }

    // Unary operators
    HighPrecisionFloat operator-() const {
        HighPrecisionFloat result(mpfr_get_prec(value));
        mpfr_neg(result.value, value, MPFR_RNDN);
        return result;
    }

    // Math functions
    HighPrecisionFloat abs() const {
        HighPrecisionFloat result(mpfr_get_prec(value));
        mpfr_abs(result.value, value, MPFR_RNDN);
        return result;
    }

    HighPrecisionFloat sqrt() const {
        HighPrecisionFloat result(mpfr_get_prec(value));
        mpfr_sqrt(result.value, value, MPFR_RNDN);
        return result;
    }

    mpfr_prec_t get_precision() const {
        return mpfr_get_prec(value);
    }

    // Raw MPFR value (for advanced users)
    mpfr_t value;
};

// ============================================================================
// High-Precision Complex Number
// ============================================================================

class HighPrecisionComplex {
public:
    HighPrecisionFloat real;
    HighPrecisionFloat imag;

    // Constructors
    explicit HighPrecisionComplex(mpfr_prec_t precision_bits = 128)
        : real(precision_bits), imag(precision_bits) {
    }

    HighPrecisionComplex(double r, double i, mpfr_prec_t precision_bits = 128)
        : real(r, precision_bits), imag(i, precision_bits) {
    }

    HighPrecisionComplex(const HighPrecisionFloat& r, const HighPrecisionFloat& i)
        : real(r), imag(i) {
    }

    // Copy constructor
    HighPrecisionComplex(const HighPrecisionComplex& other)
        : real(other.real), imag(other.imag) {
    }

    // Assignment operator
    HighPrecisionComplex& operator=(const HighPrecisionComplex& other) {
        if (this != &other) {
            real = other.real;
            imag = other.imag;
        }
        return *this;
    }

    // Convert to std::complex<double> for GPU
    std::complex<double> to_complex_double() const {
        return std::complex<double>(real.to_double(), imag.to_double());
    }

    // Complex arithmetic: z^2 = (a+bi)^2 = (a^2 - b^2) + (2ab)i
    HighPrecisionComplex square() const {
        mpfr_prec_t prec = real.get_precision();
        HighPrecisionComplex result(prec);

        // real part = real^2 - imag^2
        HighPrecisionFloat real_sq = real * real;
        HighPrecisionFloat imag_sq = imag * imag;
        result.real = real_sq - imag_sq;

        // imag part = 2 * real * imag
        HighPrecisionFloat two(2.0, prec);
        result.imag = two * real * imag;

        return result;
    }

    // Addition
    HighPrecisionComplex operator+(const HighPrecisionComplex& other) const {
        mpfr_prec_t prec = real.get_precision();
        HighPrecisionComplex result(prec);
        result.real = real + other.real;
        result.imag = imag + other.imag;
        return result;
    }

    // Subtraction
    HighPrecisionComplex operator-(const HighPrecisionComplex& other) const {
        mpfr_prec_t prec = real.get_precision();
        HighPrecisionComplex result(prec);
        result.real = real - other.real;
        result.imag = imag - other.imag;
        return result;
    }

    // Multiplication: (a+bi)(c+di) = (ac-bd) + (ad+bc)i
    HighPrecisionComplex operator*(const HighPrecisionComplex& other) const {
        mpfr_prec_t prec = real.get_precision();
        HighPrecisionComplex result(prec);

        HighPrecisionFloat ac = real * other.real;
        HighPrecisionFloat bd = imag * other.imag;
        HighPrecisionFloat ad = real * other.imag;
        HighPrecisionFloat bc = imag * other.real;

        result.real = ac - bd;
        result.imag = ad + bc;

        return result;
    }

    // Magnitude squared: |z|^2 = a^2 + b^2
    HighPrecisionFloat magnitude_squared() const {
        return real * real + imag * imag;
    }

    // Magnitude: |z| = sqrt(a^2 + b^2)
    HighPrecisionFloat magnitude() const {
        return magnitude_squared().sqrt();
    }

    mpfr_prec_t get_precision() const {
        return real.get_precision();
    }
};

// ============================================================================
// Utility Functions
// ============================================================================

// Calculate required precision bits for a given zoom level
inline mpfr_prec_t calculate_precision_bits_for_zoom(double zoom) {
    if (zoom >= 1e-14) {
        return 64;  // Standard double precision
    }

    // Rule: need ~3.32 bits per decimal digit + safety margin
    double digits_needed = -std::log10(std::abs(zoom));
    int bits = 64 + static_cast<int>(digits_needed * 3.32) + 64;

    // Clamp to reasonable range
    bits = std::max(128, std::min(bits, 4096));

    return bits;
}

// Format high-precision number for display
inline std::string to_string(const HighPrecisionFloat& value, int decimal_places = 50) {
    char* str = nullptr;
    mpfr_asprintf(&str, "%.*Rf", decimal_places, value.value);
    std::string result(str);
    mpfr_free_str(str);
    return result;
}
