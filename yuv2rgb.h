#pragma once
#include <stdint.h>
#include <tuple>

struct yuvrgb {
    typedef uint8_t itype;
    typedef uint8_t otype;
    typedef uint16_t ctype;
    const double ca, cb, cc, cd;
    ctype acoeff, bcoeff, ccoeff, dcoeff;
    yuvrgb(double ca_ = 0.2568, double cb_ = 0.0979,
           double cc_ = 0.5772, double cd_ = 0.5910): ca(ca_), cb(cb_), cc(cc_), cd(cd_) {
        const unsigned iwidth = sizeof (itype) * 8;
        const unsigned owidth = sizeof (otype) * 8;
        const unsigned cwidth = sizeof (ctype) * 8;

    }
    std::tuple<otype, otype, otype> toyuv(itype r, itype g, itype b) {
        otype y = ca * r + (1.0 - ca - cb)*g + cb * b + 0.5;
        otype u = -cc*ca*r+cc*(ca + cb - 1.0)*g + cc*(1.0 - cb)*b + 128.5;
        otype v = cd * (1.0 - ca) * r + cd * (ca + cb - 1.0) * g + cd * (-cb) * b + 128.5;
        return std::make_tuple(y,u,v);
    }
};

