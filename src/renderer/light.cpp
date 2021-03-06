#include <cstdlib>
#include "light.h"

internal float ATTENUATION_FACTOR_TO_CALC_RANGE_FOR = 0.010f;

internal float calculate_attenuation_range(float c, float l, float q)
{
    c -= 1.f / ATTENUATION_FACTOR_TO_CALC_RANGE_FOR;
    float discriminant = l * l - 4 * q * c;
    if (discriminant >= 0)
    {
        float root1 = (-l + sqrtf(discriminant)) / (2 * q);
        float root2 = (-l - sqrtf(discriminant)) / (2 * q);
        float att_range = max(root1, root2);
        return att_range;
    }
    else
    {
        return 0.f;
    }
}

float point_light_t::update_radius()
{
    radius = calculate_attenuation_range(att_constant, att_linear, att_quadratic);
    return radius;
}

float point_light_t::get_radius() const
{
    return radius;
}

float point_light_t::get_att_constant() const
{
    return att_constant;
}

void point_light_t::set_att_constant(float att_constant)
{
    point_light_t::att_constant = att_constant;
    update_radius();
}

float point_light_t::get_att_linear() const
{
    return att_linear;
}

void point_light_t::set_att_linear(float att_linear)
{
    point_light_t::att_linear = att_linear;
    update_radius();
}

float point_light_t::get_att_quadratic() const
{
    return att_quadratic;
}

void point_light_t::set_att_quadratic(float att_quadratic)
{
    point_light_t::att_quadratic = att_quadratic;
    update_radius();
}

bool point_light_t::is_b_static() const
{
    return b_static;
}

void point_light_t::set_b_static(bool b_static)
{
    point_light_t::b_static = b_static;
}

bool point_light_t::is_b_cast_shadow() const
{
    return b_cast_shadow;
}

void point_light_t::set_b_cast_shadow(bool b_cast_shadow)
{
    point_light_t::b_cast_shadow = b_cast_shadow;
}

bool point_light_t::is_b_prebaked_shadow() const
{
    return b_prebaked_shadow;
}

void point_light_t::set_b_prebaked_shadow(bool b_prebaked_shadow)
{
    point_light_t::b_prebaked_shadow = b_prebaked_shadow;
}

bool point_light_t::is_b_spotlight() const
{
    return b_spotlight;
}

void point_light_t::set_b_spotlight(bool32 b_spotlight)
{
    point_light_t::b_spotlight = b_spotlight;
}

const vec3& point_light_t::get_direction() const
{
    return direction;
}

void point_light_t::set_direction(const vec3& direction)
{
    point_light_t::direction = direction;
}

void point_light_t::set_cutoff_in_degrees(float degrees)
{
    cos_cutoff = cosf(degrees * KC_DEG2RAD);
}

void point_light_t::set_cutoff_in_radians(float radians)
{
    cos_cutoff = radians;
}
