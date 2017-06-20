/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2014 Jose Aparicio

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

#ifndef quantlib_default_latent_model_hpp
#define quantlib_default_latent_model_hpp

#include <ql/experimental/credit/basket.hpp>
#include <ql/experimental/math/latentmodel.hpp>
#include <ql/experimental/math/gaussiancopulapolicy.hpp>
#include <ql/math/distributions/binomialdistribution.hpp>

namespace QuantLib {

    /*! \brief Default event Latent Model.

     This is a model for joint default events based on a generic Latent 
      Model. It models solely the default events in a portfolio, not making any 
      reference to severities, exposures, etc...
     An implicit correspondence is stablished between the variables modelled and
     the names in the basket given by the basket and model variable access 
     indices.
     The class is parametric on the Latent Model copula.

     \todo Consider QL_REQUIRE(basket_, "No portfolio basket set.") test in 
     debug model only for performance reasons.
    */

    namespace detail {
        std::vector<Size> combination(Size n, Size i, Size j);

        extern std::vector<std::vector<int>> bn;
    }

    template<class copulaPolicy>
    class DefaultLatentModel : public LatentModel<copulaPolicy> {
        // import template members
    protected:
        using LatentModel<copulaPolicy>::factorWeights_;
        using LatentModel<copulaPolicy>::idiosyncFctrs_;
        using LatentModel<copulaPolicy>::copula_;
    public:
        using LatentModel<copulaPolicy>::inverseCumulativeY;
        using LatentModel<copulaPolicy>::cumulativeZ;
        using LatentModel<copulaPolicy>::integratedExpectedValue;// which one?
    protected:
        // not a handle, the model doesnt keep any cached magnitudes, no need
        //  for notifications, still...
        mutable std::shared_ptr<Basket> basket_;
        std::shared_ptr<LMIntegration> integration_;
    private:
        typedef typename copulaPolicy::initTraits initTraits;
    public:
        /*!
        @param factorWeights Latent model independent factors weights for each
            variable.
        @param integralType Integration type.
        @param ini Copula initialization if any.

        \warning Baskets with realized defaults not tested/WIP.
        */
        DefaultLatentModel(
                const std::vector<std::vector<Real> > &factorWeights,
                LatentModelIntegrationType::LatentModelIntegrationType integralType,
                const initTraits &ini = initTraits()
        )
                : LatentModel<copulaPolicy>(factorWeights, ini),
                  integration_(LatentModel<copulaPolicy>::IntegrationFactory::
                               createLMIntegration(factorWeights[0].size(), integralType)) {}

        DefaultLatentModel(
                const Handle <Quote> &mktCorrel,
                Size nVariables,
                LatentModelIntegrationType::LatentModelIntegrationType integralType,
                const initTraits &ini = initTraits()
        )
                : LatentModel<copulaPolicy>(mktCorrel, nVariables, ini),
                  integration_(LatentModel<copulaPolicy>::IntegrationFactory::
                               createLMIntegration(1, integralType)) {}
        /* \todo
            Add other constructors as in LatentModel for ease of use. (less
            dimensions, factors, etcc...)
        */

        /* To interface with loss models. It is possible to change the basket
        since there are no cached magnitudes.
        */
        void resetBasket(const std::shared_ptr<Basket> basket) const {
            basket_ = basket;
            // in the future change 'size' to 'liveSize'
            QL_REQUIRE(basket_->size() == factorWeights_.size(),
                       "Incompatible new basket and model sizes.");
        }

    public:
        /*! Returns the probability of default of a given name conditional on
        the realization of a given set of values of the model independent
        factors. The date at which the probability is given is implicit in the
        probability since theres not other time dependence in this model.
        @param prob Unconditional probability of default.
        @param iName desired name.
        @param mktFactors Value of LM independent factors.
        \warning Most often it is preferred to use the method below avoiding the
        cumulative inversion.
        */
        Probability conditionalDefaultProbability(Probability prob, Size iName,
                                                  const std::vector<Real> &mktFactors) const {
            // we can be called from the outside (from an integrable loss model)
            //   but we are called often at integration points. This or
            //   consider a list of friends.
#if defined(QL_EXTRA_SAFETY_CHECKS)
            QL_REQUIRE(basket_, "No portfolio basket set.");
#endif
            /*Avoid redundant call to minimum value inversion (might be \infty),
            and this independently of the copula function.
            */
            if (prob < 1.e-10) return 0.;// use library macro...
            return conditionalDefaultProbabilityInvP(
                    inverseCumulativeY(prob, iName), iName, mktFactors);
        }

    protected:
        void update() {
            if (basket_) basket_->notifyObservers();
            LatentModel<copulaPolicy>::update();
        }

    public:// open since users access it for performance on joint integrations.

        /*! Returns the probability of default of a given name conditional on
        the realization of a given set of values of the model independent
        factors. The date at which the probability is given is implicit in the
        probability since theres not other time dependent in this model.
        Same intention as above but provides a performance opportunity, if the
        integration is along the market factors (as usually is) avoids computing
        the inverse of the probability on each call.
        @param invCumYProb Inverse cumul of the unconditional probability of
          default, has to follow the same copula law for results to be coherent
        @param iName desired name.
        @param m Value of LM independent factors.
        */
        Probability conditionalDefaultProbabilityInvP(Real invCumYProb,
                                                      Size iName,
                                                      const std::vector<Real> &m) const {
            Real sumMs =
                    std::inner_product(factorWeights_[iName].begin(),
                                       factorWeights_[iName].end(), m.begin(), 0.);
            Real res = cumulativeZ((invCumYProb - sumMs) /
                                   idiosyncFctrs_[iName]);
#if defined(QL_EXTRA_SAFETY_CHECKS)
            QL_REQUIRE (res >= 0. && res <= 1.,
                        "conditional probability " << res << "out of range");
#endif

            return res;
        }

    protected:
        /*! Returns the probability of default of a given name conditional on
        the realization of a given set of values of the model independent
        factors.
        @param date The date for the probability of default.
        @param iName desired name.
        @param mktFactors Value of LM independent factors.

        Same intention as the above methods. Usage of this one is typically more
        expensive because most often the date we call this method with
        repeats itself and with this one the probability can not be cached
        outside the call.
        */
        Probability conditionalDefaultProbability(const Date &date, Size iName,
                                                  const std::vector<Real> &mktFactors) const {
            const std::shared_ptr<Pool> &pool = basket_->pool();
            Probability pDefUncond =
                    pool->get(pool->names()[iName]).
                                    defaultProbability(basket_->defaultKeys()[iName])
                            ->defaultProbability(date);
            return conditionalDefaultProbability(pDefUncond, iName, mktFactors);
        }

        /*! Conditional default probability product, intermediate step in the
            correlation calculation.*/
        Probability condProbProduct(Real invCumYProb1, Real invCumYProb2,
                                    Size iName1, Size iName2,
                                    const std::vector<Real> &mktFactors) const {
            return
                    conditionalDefaultProbabilityInvP(invCumYProb1, iName1,
                                                      mktFactors) *
                    conditionalDefaultProbabilityInvP(invCumYProb2, iName2,
                                                      mktFactors);
        }

        //! Conditional probability of n default events or more.
        // \todo: check the issuer has not defaulted.
        Real conditionalProbAtLeastNEvents(Size n, const Date &date,
                                           const std::vector<Real> &mktFactors) const;

        //! access to integration:
        const std::shared_ptr<LMIntegration> &
        integration() const { return integration_; }

    public:
        /*! Computes the unconditional probability of default of a given name.
        Trivial method for testing
        */
        Probability probOfDefault(Size iName, const Date &d) const {
            QL_REQUIRE(basket_, "No portfolio basket set.");
            const std::shared_ptr<Pool> &pool = basket_->pool();
            // avoid repeating this in the integration:
            Probability pUncond = pool->get(pool->names()[iName]).
                            defaultProbability(basket_->defaultKeys()[iName])
                    ->defaultProbability(d);
            if (pUncond < 1.e-10) return 0.;

            return integratedExpectedValue(
                    [this, pUncond, iName](const std::vector<Real> &v) {
                        return this->conditionalDefaultProbabilityInvP(inverseCumulativeY(pUncond, iName), iName, v);
                    });
        }

        /*! Pearsons' default probability correlation.
            Users should consider specialization on the copula type for specific
            distributions since that might simplify the integrations, most
            importantly if this is to be used in calibration of observations for
            factor coefficients as it is expensive to integrate directly.
        */
        Real defaultCorrelation(const Date &d, Size iNamei, Size iNamej) const;

        /*! Returns the probaility of having a given or larger number of
        defaults in the basket portfolio at a given time.
        */
        Probability probAtLeastNEvents(Size n, const Date &date) const {
            return integratedExpectedValue(
                    [this, &date, n](const std::vector<Real> &v) {
                        return this->conditionalProbAtLeastNEvents(n, date, v);
                    });

        }
    };


    //---- Defines -----------------------------------------------------------

    template<class CP>
    Real DefaultLatentModel<CP>::defaultCorrelation(const Date &d,
                                                    Size iNamei, Size iNamej) const {
        QL_REQUIRE(basket_, "No portfolio basket set.");

        const std::shared_ptr<Pool> &pool = basket_->pool();
        // unconditionals:
        Probability pi = pool->get(pool->names()[iNamei]).
                        defaultProbability(basket_->defaultKeys()[iNamei])
                ->defaultProbability(d);
        Probability pj = pool->get(pool->names()[iNamej]).
                        defaultProbability(basket_->defaultKeys()[iNamej])
                ->defaultProbability(d);
        Real pipj = pi * pj;
        Real invPi = inverseCumulativeY(pi, iNamei);
        Real invPj = inverseCumulativeY(pj, iNamej);
        // avoid repetitive calls when i=j?
        Real E1i1j; // joint default covariance term
        if (iNamei != iNamej) {
            E1i1j = integratedExpectedValue(
                    [this, invPi, invPj, iNamei, iNamej](const std::vector<Real> &v) {
                        return this->condProbProduct(invPi, invPj, iNamei, iNamej, v);
                    });
        } else {
            E1i1j = pi;
        }
        return (E1i1j - pipj) / std::sqrt(pipj * (1. - pi) * (1. - pj));
    }


    template<class CP>
    Real DefaultLatentModel<CP>::conditionalProbAtLeastNEvents(Size n,
                                                               const Date &date,
                                                               const std::vector<Real> &mktFactors) const {
        QL_REQUIRE(basket_, "No portfolio basket set.");

        /* \todo
        This algorithm traverses all permutations starting form the
        lowest one. This is inneficient, there shouldnt be any need to
        go through the invalid ones. Use combinations of n elements.

        See integration in O'Kane for homogeneous ntds.
        */
        // first position with as many defaults as desired:
        Size poolSize = basket_->size();//move to 'livesize'
        const std::shared_ptr<Pool> &pool = basket_->pool();

        // Precalc conditional probabilities
        std::vector<Probability> pDefCond;
        for (Size i = 0; i < poolSize; i++)
            pDefCond.emplace_back(conditionalDefaultProbability(
                    pool->get(pool->names()[i]).
                            defaultProbability(basket_->defaultKeys()[i])->
                            defaultProbability(date), i, mktFactors));

        Probability probNEventsOrMore = 0.;

        std::vector<Size> allIndex(poolSize, 1);
        std::partial_sum(allIndex.begin(), allIndex.end(), allIndex.begin());

        for (Size i = n; i <= poolSize; ++i) {
            Size choices;
            if (poolSize < 15) choices = detail::bn[poolSize][i];
            else choices = binomialCoefficient(poolSize, i);
            for (Size j = 1; j <= choices; ++j) {
                std::vector<Size> chosen = detail::combination(poolSize, i, j);
                std::vector<Size> unchosen(poolSize - i);
                std::set_difference(allIndex.begin(), allIndex.end(), chosen.begin(), chosen.end(), unchosen.begin());
                Probability pConfig = 1;
                for (Size k = 0; k < i; ++k)
                    pConfig *= pDefCond[chosen[k] - 1];
                for (Size k = 0; k < (poolSize - i); ++k)
                    pConfig *= (1.0 - pDefCond[unchosen[k] - 1]);
                probNEventsOrMore += pConfig;
            }
        }
        return probNEventsOrMore;
    }


    // often used:
    typedef DefaultLatentModel<GaussianCopulaPolicy> GaussianDefProbLM;
    typedef DefaultLatentModel<TCopulaPolicy> TDefProbLM;


}

#endif