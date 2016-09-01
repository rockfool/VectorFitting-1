// OpenSEMBA
// Copyright (C) 2015 Salvador Gonzalez Garcia        (salva@ugr.es)
//                    Luis Manuel Diaz Angulo         (lmdiazangulo@semba.guru)
//                    Miguel David Ruiz-Cabello Nuñez (miguel@semba.guru)
//                    Daniel Mateos Romero            (damarro@semba.guru)
//                    Alejandro García Montoro        (alejandro.garciamontoro@gmail.com)
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

#ifndef SEMBA_MATH_FITTING_VECTOR_H_
#define SEMBA_MATH_FITTING_VECTOR_H_

#include <vector>
#include <complex>
#include <eigen3/Eigen/Dense>

#include "Real.h"
#include "Options.h"

namespace VectorFitting {

using namespace Eigen;

typedef std::complex<Real> Complex;

/**
 * Samples are formed by a pair formed by:
 *  - First, the parameter $s = j \omega$ a purely imaginary number.
 *  - Second, a vector with the complex data to be fitted.
 */
typedef std::pair<Complex, std::vector<Complex>> Sample;

class VectorFitting {
public:

    /**
     * Build a fitter with starting poles computed automatically.
     * @param samples   Data to be fitted.
     * @param order     Order of approximation.
     * @param options   Options.
     */
    VectorFitting(
            const std::vector<Sample>& samples,
            const size_t order,
            const Options& options);

    /**
     * Build a fitter with starting poles provided by the user. order_ and
     * poles.size() shall be the same
     * @param samples   Data to be fitted.
     * @param poles     Starting poles.
     * @param options   Options.
     */
    VectorFitting(const std::vector<Sample>& samples,
                  const std::vector<Complex>& poles,
                  const Options& options);

    // This could be called from the constructor, but if an iterative algorithm
    // is preferred, it's a good idea to have it as a public method
    void fit();

    std::vector<Complex> predictResponse(Complex freq) const;
    std::vector<Sample>  getFittedSamples(std::vector<Complex> freqs) const;
    std::vector<Complex> getPoles();

    /**
     *  Getters to fitting coefficents.
     */
    MatrixXcd getA() {return A_;}    // Size: Nc, N.
    MatrixXcd getC() {return C_;}    // Size: Nc, N.
    RowVectorXcd getB() {return B_;} // Size: N,  1.
    RowVectorXcd getD() {return D_;} // Size: Nc, 1.
    RowVectorXcd getE() {return E_;} // Size: Nc, 1.
    Real getRMSE();

private:
    Options options_;

    std::vector<Sample> samples_;
    std::vector<Complex> poles_;

    MatrixXcd  A_,C_;
    RowVectorXcd B_, D_, E_;

    MatrixXd weights_; // Size: Ns, Nc

    void init(const std::vector<Sample>& samples,
              const std::vector<Complex>& poles,
              const Options& options);

    size_t getSamplesSize() const;
    size_t getResponseSize() const;
    size_t getOrder() const;
};

} /* namespace VectorFitting */

#endif // SEMBA_MATH_FITTING_VECTOR_H_
