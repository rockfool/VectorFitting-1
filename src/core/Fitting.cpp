// OpenSEMBA
// Copyright (C) 2015 Salvador Gonzalez Garcia        (salva@ugr.es)
//                    Luis Manuel Diaz Angulo         (lmdiazangulo@semba.guru)
//                    Miguel David Ruiz-Cabello Nuñez (miguel@semba.guru)
//                    Alejandro García Montoro        (alejandro.garciamontoro@gmail.com)
//					  Alejandra López de Aberasturi Gómez (aloaberasturi@ugr.es)
//
// This file is part of OpenSEMBA.
//
// OpenSEMBA is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// OpenSEMBA is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
// details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with OpenSEMBA. If not, see <http://www.gnu.org/licenses/>.

#include "Fitting.h"

#include "SpaceGenerator.h"

namespace VectorFitting {

Fitting::Fitting(
        const std::vector<Sample>& samples,
        const Options& options,
        const std::vector<Complex>& poles,
		const std::vector<VectorXd>& weights) :
                options_(options),
                samples_(samples),
                poles_(poles),
                weights_(weights) {
    std::sort(samples_.begin(), samples_.end(), [](Sample a, Sample b) {
        return lower(a.first.imag(), b.first.imag());
    });

    if (poles_.empty()) {
        throw std::runtime_error("Poles size can not be zero.");
    }

    if (samples_.empty()) {
        throw std::runtime_error("Samples size cannot be zero");
    }

    if (!weights_.empty() && weights_.size() != samples_.size()) {
        throw std::runtime_error("Weights and samples must have same size.");
    }
    if (weights_.empty()) {
        weights_ = std::vector<VectorXd>(
                getSamplesSize(), VectorXd::Ones(getResponseSize()));
    }
    // Sanity check: the complex poles should come in pairs; otherwise, there
    // is an error
    Complex currentPole;
    for (size_t i = 0; i < poles_.size(); i++) {
        currentPole = poles_[i];
        if(!isReal(currentPole)){
            if (conj(currentPole) == poles_[i+1]) {
                i++;
            } else {
                throw std::runtime_error(
                   "Poles with imaginary parts must be complex conjugated");
            }
        }
    }
}


void Fitting::fit(){
    // Following Gustavssen notation in vectfit3.m .
    const size_t Ns = getSamplesSize();
    const size_t N  = getOrder();
    const size_t Nc = getResponseSize();

    VectorXcd SERD(Nc), SERE(Nc);
    VectorXi SERB(N);
    RowVectorXcd SERA(1,N);
    MatrixXcd  SERC(Nc, N);
    for (size_t i = 0; i < N; ++i) {
        SERA(0,i) = poles_[i];
    }
    VectorXcd roetter;

    // --- Pole identification ---
    if (!options_.isSkipPoleIdentification()) {

        // Finds out which starting poles are complex.
        RowVectorXi cindex = getCIndex(poles_);

        // Builds system - matrix.
        MatrixXcd LAMBD = MatrixXcd::Zero(N, N);
        for (size_t i = 0; i < N; ++i) {
            LAMBD(i,i) = poles_[i];
        }

        MatrixXcd Dk;
        switch (options_.getAsymptoticTrend()) {
        case Options::AsymptoticTrend::zero:
        case Options::AsymptoticTrend::constant:
            Dk = MatrixXcd::Zero(Ns,N+1);
            break;
        case Options::AsymptoticTrend::linear:
            Dk = MatrixXcd::Zero(Ns,N+2);
            break;
        }

        MatrixXcd LAMBDprime = LAMBD.transpose().conjugate();
        for (size_t m = 0; m < N; ++m) {
            if (cindex(m) == 0) { // Real pole.
                for (size_t i = 0; i < Ns; ++i) {
                    Dk(i,m) = Complex(1,0) / (samples_[i].first - LAMBD(m,m));
                }
            } else if (cindex(m) == 1) { // Complex pole, first part.
                for (size_t i = 0; i < Ns; ++i) {
                    Dk(i,m)   = Complex(1,0) / (samples_[i].first - LAMBD(m,m))
                               + Complex(1,0) / (samples_[i].first - LAMBDprime(m,m));
                    Dk(i,m+1) = Complex(0,1) / (samples_[i].first - LAMBD(m,m))
                               - Complex(0,1) / (samples_[i].first - LAMBDprime(m,m));
                }
            }
        }
        for (size_t i = 0; i < Ns; ++i) {
            Dk(i,N) = (Real) 1.0;
            if (options_.getAsymptoticTrend() ==
            		Options::AsymptoticTrend::linear) {
                Dk(i,N+1) = samples_[i].first;
            }
        }
        // Scaling for last row of LS-problem (pole identification).
        Real scale = 0.0;
        for (size_t m = 0; m < Nc; ++m) {
            for (size_t i = 0; i < Ns; ++i) {
                const Complex sample = samples_[i].second[m];
                scale += std::pow(std::abs(
                        useWeight_(i,m) * std::conj(sample)), 2);
            }
        }
        scale = std::sqrt(scale) / (Real) Ns;

        size_t offs;
        switch (options_.getAsymptoticTrend()) {
        case Options::AsymptoticTrend::zero:
            offs = 0;
            break;
        case Options::AsymptoticTrend::constant:
            offs = 1;
            break;
        case Options::AsymptoticTrend::linear:
            offs = 2;
            break;
        }

        VectorXd x(N+1);
        if (options_.isRelax()) {

            // Computes AA and bb. Corresponding line in vectfit3.m code: 319
            MatrixXd AA = MatrixXd::Zero(Nc*(N+1), N+1);
            VectorXd bb = VectorXd::Zero(Nc*(N+1));
            for (size_t n = 0; n < Nc; ++n) {
                MatrixXd A = MatrixXd::Zero(2*Ns+1, (N+offs)+N+1);
                // Left block.
                for (size_t m = 0; m < N + offs; ++m) {
                    for (size_t i = 0; i < Ns; ++i) {
                        const Complex entry = useWeight_(i,n) * Dk(i,m);
                        A(i   ,m) = std::real(entry);
                        A(i+Ns,m) = std::imag(entry);
                    }
                }
                // Right block.
                const size_t inda = N + offs;
                for (size_t m = 0; m < N+1; ++m) {
                    for (size_t i = 0; i < Ns; ++i) {
                        const Complex entry =
                         - useWeight_(i,n) * Dk(i,m) * samples_[i].second[n];
                        A(i   ,inda+m) = std::real(entry);
                        A(i+Ns,inda+m) = std::imag(entry);
                    }
                }

                // Integral criterion for sigma.
                const size_t offset = N + offs;
                if (n == Nc-1) {
                    for (size_t mm = 0; mm < N+1; ++mm) {
                        A(2*Ns, offset+mm) = std::real(scale*Dk.col(mm).sum());
                    }
                }

                // Performs QR decomposition. Line 350
                MatrixXd Q, R;
                HouseholderQR<MatrixXd> qr(A.rows(), A.cols());
                qr.compute(A);
                Q = qr.householderQ() * MatrixXd::Identity(A.rows(),A.cols());
                R = Q.transpose() * A;

                const size_t ind = N + offs;
                MatrixXd R22 = R.block(ind,ind, N+1,N+1);
                AA.block(n*(N+1), 0, N+1, N+1) = R22;
                if (n == Nc-1) {
                    for (size_t i = 0; i < N+1; ++i) {
                        bb(i + n*(N+1)) = Q(2*Ns, N + offs + i)
                                * (Real) Ns * (Real) scale;
                    }
                }
            }  // End of for loop n=1:Nc

            // Computes scaling factor. Line 360
            VectorXd Escale = VectorXd::Zero(N+1);
            for (size_t col = 0; col < N+1; ++col) {
                const double norm = AA.col(col).norm();
                Escale(col) = 1.0 / norm;
                AA.col(col) *= Escale(col);
            }

            x = AA.householderQr().solve(bb);
            for (size_t i = 0; i < N+1; ++i) {
                x(i) *= Escale(i);
            }

        } // End of if for "relax" flag.

        if (!options_.isRelax() //Line 372
                || lower  (std::abs(x(N)), toleranceLow_)
                || greater(std::abs(x(N)), toleranceHigh_) ) {

            Real Dnew;
            if (!options_.isRelax()) {
                Dnew = 1.0;
            } else {
                if (std::abs(x(N)) < toleranceLow_) {
                    Dnew = 1.0;
                } else if (lower  (std::abs(x(N)), toleranceLow_)) {
                    std::signbit(x(N)) ?
                            Dnew = toleranceLow_ : Dnew  = - toleranceLow_;
                } else if (greater(std::abs(x(N)), toleranceHigh_)) {
                    std::signbit(x(N)) ?
                            Dnew = toleranceHigh_ : Dnew = - toleranceHigh_;
                } else {
                    throw std::runtime_error("Can not relax constant term");
                }
            }

            MatrixXd AA(Nc*N, N);
            VectorXd bb(Nc*N);
            for (size_t n = 0; n < Nc; ++n) {

                MatrixXd A(2*Ns, N+offs+N);
                {
                    for (size_t m = 0; m < N+offs; ++m) {
                        for (size_t i = 0; i < Ns; ++i) {
                            A(i,m)     = std::real(useWeight_(i,n) * Dk(i,m));
                            A(i+Ns, m) = std::imag(useWeight_(i,n) * Dk(i,m));
                        }
                    }
                    MatrixXd::Index inda = N + offs;
                    for (size_t m = 0; m < N; ++m) {
                        for (size_t i = 0; i < Ns; ++i) {
                            Complex aux = - useWeight_(i,n) *
                                    Dk(i,m) * std::conj(samples_[i].second(n));
                            A(i,    inda+m) = std::real(aux);
                            A(i+Ns, inda+m) = std::imag(aux);
                        }
                    }
                }

                VectorXd b(2*Ns);
                for (size_t i = 0; i < Ns; ++i) {
                    Complex aux = Dnew *
                            useWeight_(i,n) * std::conj(samples_[i].second(n));
                    b(i)    = std::real(aux);
                    b(i+Ns) = std::imag(aux);
                }

                HouseholderQR<MatrixXd> qr(A.rows(), A.cols());
                qr.compute(A);
                MatrixXd Q = qr.householderQ() *
                        MatrixXd::Identity(A.rows(),A.cols());
                MatrixXd R = Q.transpose() * A;
                MatrixXd::Index ind = N+offs;
                MatrixXd R22 = R.block(ind,ind, N,N);
                AA.block(n*N, 0, N, N) = R22;
                bb.block(n*N, 0, N, 1) =
                        Q.block(0, ind, A.rows(), N).transpose() * b;
            }

            RowVectorXd Escale = RowVectorXd::Zero(N);
            for (MatrixXd::Index col = 0; col < AA.cols(); ++col) {
                Escale(col) = 1 / AA.col(col).norm();
                AA.col(col) *= Escale(col);
            }

            VectorXd xAux = AA.householderQr().solve(bb);
            for (VectorXd::Index i = 0; i < xAux.size(); ++i) {
                xAux(i) *= Escale(i);
            }

            x.head(N) = xAux;
            x(N) = Dnew;
        }

        VectorXcd C = x.head(N).cast<Complex>(); // Line 433
        for (size_t m = 0; m < N; ++m) {
            if (cindex(m) == 1) {
                const Real r1 = std::real(C(m  ));
                const Real r2 = std::real(C(m+1));
                C(m)   = Complex(r1,  r2);
                C(m+1) = Complex(r1, -r2);
            }
        }
        Real D = x(N);

        // Calculates the zeros for sigma. Line 481
        VectorXi B = VectorXi::Ones(N);
        size_t m = 0;
        for (size_t n = 0; n < N; ++n) {
            if (m < N) {
                if (greater(std::abs(LAMBD(m,m)),
                            std::abs(std::real(LAMBD(m,m))))) {
                    LAMBD(m+1,m  ) = - std::imag(LAMBD(m,m));
                    LAMBD(m  ,m+1) =   std::imag(LAMBD(m,m));
                    LAMBD(m  ,m  ) =   std::real(LAMBD(m,m));
                    LAMBD(m+1,m+1) =             LAMBD(m,m);
                    B(m  ) = 2;
                    B(m+1) = 0;
                    const Complex aux = C(m);
                    C(m  ) = std::real(aux);
                    C(m+1) = std::imag(aux);
                    m++;
                }
            }
            m++;
        }

        // Checks LAMBD and C are purely real.
        for (size_t i = 0; i < N; ++i) {
            for (size_t j = 0; j < N; ++j) {
                if (!equal(std::imag(LAMBD(i,j)), 0.0)) {
                    throw std::runtime_error("LAMBD is not purely real");
                }
            }
        }
        for (size_t i = 0; i < N; ++i) {
            if (!equal(std::imag(C(i)), 0.0)) {
                throw std::runtime_error("LAMBD is not purely real");
            }
        }

        MatrixXd ZER = MatrixXd::Zero(N,N);//Line 498
        for (size_t i = 0; i < N; ++i) {
        	for (size_t j = 0; j < N; ++j) {
        		ZER(i,j) = std::real(LAMBD(i,j)) - (Real) B(i) * std::real(C(j)) / D;
        	}
        }

        // Stores roetter. Lines 499-504
        roetter = EigenSolver<MatrixXd>(ZER, false).eigenvalues();
        if (options_.isStable()) {
        	for (size_t i = 0; i < N; ++i) {
        		const Real realPart = std::real(roetter(i));
        		if (greater(realPart, 0.0)) {
        			roetter(i) = roetter(i) - 2.0 * realPart;
        		}
        	}
        }

        // First pure real poles in ascending order.
        // Then complex poles in ascending order by imaginary part.
       // lines 508 - 524
        std::vector<Real> auxReal;
        std::vector<Complex> auxComplex;
        for (size_t m = 0; m < N; ++m) {
            if (equal(roetter(m).imag(), 0.0)) {
                auxReal.push_back(roetter(m).real());
            } else {
                auxComplex.push_back(roetter(m));
            }
        }
        std::sort(auxReal.begin(), auxReal.end());
        std::sort(auxComplex.begin(), auxComplex.end(), complexOrdering);
        for (size_t m = 0; m < auxReal.size(); ++m) {
            roetter(m) = auxReal[m];
        }
        for (size_t m = 0; m < auxComplex.size(); ++m) {
            roetter(m + auxReal.size()) = auxComplex[m];
        }
        // Stores results for poles.
        SERA = roetter;

    } // End of if for "skip pole identification" flag.

    // --- Residue identification ---
    if (!options_.isSkipResidueIdentification()) {
        // We now calculate SER for f, using the modified zeros of sigma
        // as new poles.
        const VectorXcd& LAMBD = roetter;
        RowVectorXi cindex = getCIndex(toStdVector(LAMBD));

        // We now calculate the SER for f (new fitting), using the above
        // calculated zeros as known poles.
        MatrixXcd Dk = MatrixXcd::Zero(Ns,N);
        for (size_t m = 0; m < N; ++m) {
            for (size_t i = 0; i < Ns; ++i) {
                if (cindex(m) == 0) {
                    Dk(i,m) = Complex(1,0) / (samples_[i].first - LAMBD(m));
                } else if (cindex(m) == 1) {
                    Dk(i,m)   = Complex(1,0) / (samples_[i].first - LAMBD(m)) +
                            Complex(1,0) / (samples_[i].first - std::conj(LAMBD(m)));
                    Dk(i,m+1) = Complex(0,1) / (samples_[i].first - LAMBD(m)) -
                            Complex(0,1) / (samples_[i].first - std::conj(LAMBD(m)));
                }
            }
        }

        MatrixXcd C  = MatrixXcd::Zero(Nc,N);
        for (size_t n = 0; n < Nc; ++n) {
            VectorXcd BB = VectorXcd::Zero(2*Ns);
            MatrixXcd A;
            switch (options_.getAsymptoticTrend()) {
            case Options::AsymptoticTrend::zero:
                A = MatrixXcd::Zero(2*Ns, N);
                break;
            case Options::AsymptoticTrend::constant:
                A = MatrixXcd::Zero(2*Ns, N+1);
                break;
            case Options::AsymptoticTrend::linear:
                A = MatrixXcd::Zero(2*Ns, N+2);
                break;
            }
            for (size_t i = 0; i < Ns; ++i) {
                for (size_t j = 0; j < N; ++j) {
                    A (i    ,j) =   std::real(Dk(i,j)) * useWeight_(i,n);
                    A (i+Ns ,j) =   std::imag(Dk(i,j)) * useWeight_(i,n);
                    BB(i)    = std::real(samples_[i].second(n)) * useWeight_(i,n);
                    BB(i+Ns) = std::imag(samples_[i].second(n)) * useWeight_(i,n);
                }
            }
            switch (options_.getAsymptoticTrend()) {
            case Options::AsymptoticTrend::zero:
                break;
            case Options::AsymptoticTrend::constant:
                for (size_t i = 0; i < Ns; ++i) {
                    A(i,    N) = 1.0 * useWeight_(i,n);
                    A(i+Ns, N) = 0.0 * useWeight_(i,n);
                }
                break;
            case Options::AsymptoticTrend::linear:
                for (size_t i = 0; i < Ns; ++i) {
                    A(i,    N  ) = 1.0 * useWeight_(i,n);
                    A(i+Ns, N  ) = 0.0 * useWeight_(i,n);
                    A(i,    N+1) = std::real(samples_[i].first) * useWeight_(i,n);
                    A(i+Ns, N+1) = std::imag(samples_[i].first) * useWeight_(i,n);
                }
                break;
            }

            // Computes scaling factor.Line 624
            VectorXd Escale(A.cols());
            for (int col = 0; col < A.cols(); ++col) {
                Escale(col) = A.col(col).norm();
                A.col(col) /= Escale(col);
            }

            MatrixXcd X = A.householderQr().solve(BB);
            for (int i = 0; i < A.cols(); ++i) {
                X(i) /= Escale(i);
            }

            // Stores results for response;
            for (size_t i = 0; i < N; ++i) {
                C(n,i) = X(i);
            }
            switch (options_.getAsymptoticTrend()) {
            case Options::AsymptoticTrend::zero:
            {
                SERD(n) = 0.0;
                SERE(n) = 0.0;
                break;
            }
            case Options::AsymptoticTrend::constant:
            {
                SERD(n) = X(N);
                SERE(n) = 0.0;
                break;
            }
            case Options::AsymptoticTrend::linear:
            {
                SERD(n) = X(N);
                SERE(n) = X(N+1);
                break;
            }
            }
        } // End of loop over Nc responses.Line 696

        for (size_t m = 0; m < N; ++m) {
            if (cindex(m) == 1) {
                for (size_t n = 0; n < Nc; ++n) {
                    const Real r1 = std::real(C(n, m  ));
                    const Real r2 = std::real(C(n, m+1));
                    C(n, m  ) = Complex(r1,  r2);
                    C(n, m+1) = Complex(r1, -r2);
                }
            }
        }

        SERA = LAMBD;
        SERB = VectorXi::Ones(N);
        SERC = C;
    } // End of if for "skip residue identification" flag.


    //Line 812

    A_ = MatrixXcd::Zero(N,N);
    for (size_t i = 0; i < N; ++i) {
        A_(i,i) = SERA(i);
        poles_[i] = SERA(i);
    }
    if (!options_.isSkipResidueIdentification()) {
        B_ = SERB;
        C_ = SERC;
        D_ = SERD;
        E_ = SERE;
    } else {
        B_ = VectorXi::Ones(N);
        C_ = MatrixXcd::Zero(Nc, N);
        D_ = VectorXcd::Zero(Nc);
        E_ = VectorXcd::Zero(Nc);
    }

    //Line 819

    // Converts into real state-space model
    if (!options_.isComplexSpaceState()) {
        RowVectorXi cindex = getCIndex(poles_);
        size_t n = 0;
        for (size_t m = 0; m < N; ++m) {
            if (cindex(m) == 1) {
                Real a1 = std::real(A_(n,n));
                Real a2 = std::imag(A_(n,n));
                VectorXcd c1(Nc), c2(Nc);
                for (size_t i = 0; i < Nc; ++i) {
                    c1(i) = std::real(C_(i,n));
                    c2(i) = std::imag(C_(i,n));
                }
                Real b1 =   2.0 * std::real(B_(n));
                Real b2 = - 2.0 * std::imag(B_(n));
                Matrix2cd Ablock;
                Ablock(0,0) =   a1;
                Ablock(0,1) =   a2;
                Ablock(1,0) = - a2;
                Ablock(1,1) =   a1;
                A_.block(n,n,2,2) = Ablock;
                C_.block(0,   n, Nc, 1) = c1;
                C_.block(0, n+1, Nc, 1) = c2;
                B_(n  ) = b1;
                B_(n+1) = b2;
            }
            n++;
        }
    }
}

/**
 * Return the fitted samples: a vector of pairs s <-> f(s), where f(s) is
 * computed with the model in (2).
 * @return A std::vector of Samples obtained with the fitted parameters.
 */
std::vector<Fitting::Sample> Fitting::getFittedSamples() const {
    const size_t N  = getOrder();
    const size_t Ns = getSamplesSize();
    const size_t Nc = getResponseSize();

    MatrixXcd Dk = MatrixXcd::Zero(Ns,N);
    for (size_t m = 0; m < N; ++m) {
        for (size_t i = 0; i < Ns; ++i) {
            Dk(i,m) = Complex(1.0, 0) / (samples_[i].first - poles_[m]);
        }
    }

    std::vector<Sample> res(
            Ns, Sample(Complex(0.0,0.0), VectorXcd(Nc)));
    MatrixXcd fit = MatrixXcd::Zero(Nc,Ns);

    for (size_t n = 0; n < Nc; ++n) {
        fit.block(n,0,1,Ns) = (Dk * C_.block(n,0,1,N).transpose()).transpose();
        for (size_t i = 0; i < Ns; ++i) {
            fit(n,i) += D_(n) + samples_[i].first * E_(n);
        }
        for (size_t i = 0; i < Ns; ++i) {
            res[i].first = samples_[i].first;
            res[i].second[n] = fit(n,i);
        }
    }
    return res;
}

std::vector<Complex> Fitting::getPoles() {
    return poles_;
}

/**
 * Returns the error of the model, measured as the root mean
 * square of the estimated data with respect to the samples.
 * @return Real - Root mean square error of the model.
 */
Real Fitting::getRMSE() const {
    std::vector<Sample> fittedSamples = getFittedSamples();

    Real error = 0.0;
    for (size_t i = 0; i < samples_.size(); i++) {
        for (VectorXcd::Index j = 0; j < samples_[i].second.size(); j++) {
            Complex actual = samples_[i].second[j];
            Complex fitted = fittedSamples[i].second[j];
            Complex diff = actual - fitted;
            error += abs(diff * diff);
        }
    }

    return sqrt(error/((Real)(samples_.size() * samples_.size())));
}

Real Fitting::getMaxDeviation() const {
    std::vector<Sample> fittedSamples = getFittedSamples();
    std::vector<Real> dev(fittedSamples.size(), 0.0);

    for (size_t i = 0; i < fittedSamples.size(); ++i) {
        std::vector<Real> sampleDev(getResponseSize(), 0.0);
        for (size_t j = 0; j < getResponseSize(); ++j) {
            sampleDev[j] = std::abs(
                    samples_[i].second[j] - fittedSamples[i].second[j]);
        }
        dev[i] = *std::max_element(sampleDev.begin(), sampleDev.end());
    }
    return *std::max_element(dev.begin(), dev.end());
}

size_t Fitting::getSamplesSize() const {
    return samples_.size();
}

size_t Fitting::getResponseSize() const {
    if (samples_.size() == 0) {
    	throw std::runtime_error("Response size is equal to zero");
    }
    return samples_.front().second.size();
}

size_t Fitting::getOrder() const {
    return (size_t) poles_.size();
}

RowVectorXi Fitting::getCIndex(const std::vector<Complex>& poles) {
    const size_t N = poles.size();
    RowVectorXi cindex = RowVectorXi::Zero(N);
    for (size_t m = 0; m < N; ++m) {
        if (!equal(std::imag(poles[m]), 0.0)) {
            if (m == 0) {
                cindex(m) = 1;
            } else {
                if (cindex(m-1) == 0 || cindex(m-1) == 2) {
                    cindex(m) = 1;
                    cindex(m+1) = 2;
                } else {
                    cindex(m) = 2;
                }
            }
        }
    }
    return cindex;
}

} /* namespace VectorFitting */

