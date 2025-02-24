#pragma once

#include <vector>
#include "glm/vec2.hpp"

// clipper from geojson-vt-cpp

using real = float;
using vt_point = glm::vec2;
constexpr real REAL_MAX = std::numeric_limits<real>::max();

template <uint8_t I, typename T>
inline real get(const T&);

template <>
inline real get<0>(const vt_point& p) {
    return p.x;
}
template <>
inline real get<1>(const vt_point& p) {
    return p.y;
}

template <uint8_t I>
inline vt_point intersect(const vt_point&, const vt_point&, const real);

template <>
inline vt_point intersect<0>(const vt_point& a, const vt_point& b, const real x) {
    const real y = (x - a.x) * (b.y - a.y) / (b.x - a.x) + a.y;
    return { x, y };  //, 1.0 };
}
template <>
inline vt_point intersect<1>(const vt_point& a, const vt_point& b, const real y) {
    const real x = (y - a.y) * (b.x - a.x) / (b.y - a.y) + a.x;
    return { x, y };  //, 1.0 };
}

// using vt_multi_point = std::vector<vt_point>;

struct vt_line_string : std::vector<vt_point> {
    using container_type = std::vector<vt_point>;
    using container_type::container_type;

    vt_line_string(const std::vector<vt_point>& lr) : container_type(lr) {}
    vt_line_string(std::vector<vt_point>&& lr) noexcept : container_type(std::move(lr)) {}
    //real dist = 0.0; // line length
};

struct vt_linear_ring : std::vector<vt_point> {
    using container_type = std::vector<vt_point>;
    using container_type::container_type;

    vt_linear_ring(const std::vector<vt_point>& ls) : container_type(ls) {}
    vt_linear_ring(std::vector<vt_point>&& ls) noexcept : container_type(std::move(ls)) {}
    //real area = 0.0; // polygon ring area
};

using vt_multi_line_string = std::vector<vt_line_string>;
using vt_polygon = std::vector<vt_linear_ring>;
using vt_multi_polygon = std::vector<vt_polygon>;

/* clip features between two axis-parallel lines:
 *     |        |
 *  ___|___     |     /
 * /   |   \____|____/
 *     |        |
 */

template <uint8_t I>
class clipper {
public:
    const real k1;
    const real k2;

    /*vt_geometry operator()(const vt_point& point) const {
        return point;
    }

    vt_geometry operator()(const vt_multi_point& points) const {
        vt_multi_point part;
        for (const auto& p : points) {
            const real ak = get<I>(p);
            if (ak >= k1 && ak <= k2)
                part.push_back(p);
        }
        return part;
    }*/

    vt_multi_line_string operator()(const vt_line_string& line) const {
        vt_multi_line_string parts;
        clipLine(line, parts);
        return parts;
    }

    vt_multi_line_string operator()(const vt_multi_line_string& lines) const {
        vt_multi_line_string parts;
        for (const auto& line : lines) {
            clipLine(line, parts);
        }
        return parts;
    }

    vt_polygon operator()(const vt_polygon& polygon) const {
        vt_polygon result;
        for (const auto& ring : polygon) {
            const auto new_ring = clipRing(ring);
            if (!new_ring.empty())
                result.push_back(new_ring);
        }
        return result;
    }

    vt_multi_polygon operator()(const vt_multi_polygon& polygons) const {
        vt_multi_polygon result;
        for (const auto& polygon : polygons) {
            vt_polygon p;
            for (const auto& ring : polygon) {
                const auto new_ring = clipRing(ring);
                if (!new_ring.empty())
                    p.push_back(new_ring);
            }
            if (!p.empty())
                result.push_back(p);
        }
        return result;
    }

private:
    vt_line_string newSlice(vt_multi_line_string& parts, vt_line_string& slice) const {  //, real dist
        if (!slice.empty()) {
            //slice.dist = dist;
            parts.push_back(std::move(slice));
        }
        return {};
    }

    void clipLine(const vt_line_string& line, vt_multi_line_string& slices) const {

        //const real dist = line.dist;
        const size_t len = line.size();

        if (len < 2)
            return;

        vt_line_string slice;

        for (size_t i = 0; i < (len - 1); ++i) {
            const auto& a = line[i];
            const auto& b = line[i + 1];
            const real ak = get<I>(a);
            const real bk = get<I>(b);

            if (ak < k1) {
                if (bk > k2) { // ---|-----|-->
                    slice.push_back(intersect<I>(a, b, k1));
                    slice.push_back(intersect<I>(a, b, k2));
                    slice = newSlice(slices, slice);  //, dist);

                } else if (bk >= k1) { // ---|-->  |
                    slice.push_back(intersect<I>(a, b, k1));
                    if (i == len - 2)
                        slice.push_back(b); // last point
                }
            } else if (ak > k2) {
                if (bk < k1) { // <--|-----|---
                    slice.push_back(intersect<I>(a, b, k2));
                    slice.push_back(intersect<I>(a, b, k1));
                    slice = newSlice(slices, slice);  //, dist);

                } else if (bk <= k2) { // |  <--|---
                    slice.push_back(intersect<I>(a, b, k2));
                    if (i == len - 2)
                        slice.push_back(b); // last point
                }
            } else {
                slice.push_back(a);

                if (bk < k1) { // <--|---  |
                    slice.push_back(intersect<I>(a, b, k1));
                    slice = newSlice(slices, slice);  //, dist);

                } else if (bk > k2) { // |  ---|-->
                    slice.push_back(intersect<I>(a, b, k2));
                    slice = newSlice(slices, slice);  //, dist);

                } else if (i == len - 2) { // | --> |
                    slice.push_back(b);
                }
            }
        }

        // add the final slice
        newSlice(slices, slice);  //, dist);
    }

    vt_linear_ring clipRing(const vt_linear_ring& ring) const {
        const size_t len = ring.size();

        vt_linear_ring slice;
        //slice.area = ring.area;

        if (len < 2)
            return slice;

        for (size_t i = 0; i < (len - 1); ++i) {
            const auto& a = ring[i];
            const auto& b = ring[i + 1];
            const real ak = get<I>(a);
            const real bk = get<I>(b);

            if (ak < k1) {
                if (bk >= k1) {
                    slice.push_back(intersect<I>(a, b, k1)); // ---|-->  |
                    if (bk > k2)                             // ---|-----|-->
                        slice.push_back(intersect<I>(a, b, k2));
                    else if (i == len - 2)
                        slice.push_back(b); // last point
                }
            } else if (ak > k2) {
                if (bk <= k2) { // |  <--|---
                    slice.push_back(intersect<I>(a, b, k2));
                    if (bk < k1) // <--|-----|---
                        slice.push_back(intersect<I>(a, b, k1));
                    else if (i == len - 2)
                        slice.push_back(b); // last point
                }
            } else {
                slice.push_back(a);
                if (bk < k1) // <--|---  |
                    slice.push_back(intersect<I>(a, b, k1));
                else if (bk > k2) // |  ---|-->
                    slice.push_back(intersect<I>(a, b, k2));
                // | --> |
            }
        }

        // close the polygon if its endpoints are not the same after clipping
        if (!slice.empty()) {
            const auto& first = slice.front();
            const auto& last = slice.back();
            if (first != last) {
                slice.push_back(first);
            }
        }

        return slice;
    }
};
