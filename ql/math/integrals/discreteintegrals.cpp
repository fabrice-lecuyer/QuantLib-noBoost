/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2014 Klaus Spanderen

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql/math/integrals/discreteintegrals.hpp>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++11-extensions"
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace QuantLib {
    Real DiscreteTrapezoidIntegral::operator()(
        const Array& x, const Array& f)    const {

        const Size n = f.size();
        QL_REQUIRE(n == x.size(), "inconsistent size");

        Real acc = 0;

        for (Size i=0; i < n-1; ++i) {
            acc += (x[i+1]-x[i])*(f[i]+f[i+1]);
        }

        return 0.5*acc;
    }

    Real DiscreteSimpsonIntegral::operator()(
        const Array& x, const Array& f)    const {

        const Size n = f.size();
        QL_REQUIRE(n == x.size(), "inconsistent size");

	long double acc = 0;

        for (Size j=0; j < n-2; j+=2) {
            const long double dxj   = x[j+1]-x[j];
            const long double dxjp1 = x[j+2]-x[j+1];

            const long double alpha = -dxjp1*(2*x[j]-3*x[j+1]+x[j+2]);
            const long double dd = x[j+2]-x[j];
            const long double k = dd/(6*dxjp1*dxj);
            const long double beta = dd*dd;
            const long double gamma = dxj*(x[j]-3*x[j+1]+2*x[j+2]);

            acc += k*alpha*f[j]+k*beta*f[j+1]+k*gamma*f[j+2];
        }
        if (!(n & 1)) {
            acc += 0.5*(x[n-1]-x[n-2])*(f[n-1]+f[n-2]);
        }

        return acc;
    }


    Real DiscreteTrapezoidIntegrator::integrate(
        std::function<Real (Real)> f, Real a, Real b) const {
            const Array x(maxEvaluations(), a, (b-a)/(maxEvaluations()-1));
            Array fv(x.size());
            std::transform(x.begin(), x.end(), fv.begin(), f);

            increaseNumberOfEvaluations(maxEvaluations());
            return DiscreteTrapezoidIntegral()(x, fv);
    }

    Real DiscreteSimpsonIntegrator::integrate(
        std::function<Real (Real)> f, Real a, Real b) const {
            const Array x(maxEvaluations(), a, (b-a)/(maxEvaluations()-1));
            Array fv(x.size());
            std::transform(x.begin(), x.end(), fv.begin(), f);

            increaseNumberOfEvaluations(maxEvaluations());
            return DiscreteSimpsonIntegral()(x, fv);
    }
}
