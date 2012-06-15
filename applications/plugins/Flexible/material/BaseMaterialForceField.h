/******************************************************************************
*       SOFA, Simulation Open-Framework Architecture, version 1.0 RC 1        *
*                (c) 2006-2011 MGH, INRIA, USTL, UJF, CNRS                    *
*                                                                             *
* This library is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This library is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this library; if not, write to the Free Software Foundation,     *
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.          *
*******************************************************************************
*                               SOFA :: Plugins                               *
*                                                                             *
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#ifndef SOFA_BaseMaterialFORCEFIELD_H
#define SOFA_BaseMaterialFORCEFIELD_H

#include "../initFlexible.h"
#include <sofa/core/behavior/ForceField.h>
#include <sofa/core/behavior/ForceField.inl>
#include <sofa/core/MechanicalParams.h>
#include <sofa/core/behavior/MechanicalState.h>

#include "../material/BaseMaterial.h"
#include "../quadrature/BaseGaussPointSampler.h"

#include <sofa/component/linearsolver/EigenSparseMatrix.h>

namespace sofa
{
namespace component
{
namespace forcefield
{

using helper::vector;

/** Abstract forcefield using MaterialBlocks or sparse eigen matrix
*/

template <class MaterialBlockType>
class SOFA_Flexible_API BaseMaterialForceField : public core::behavior::ForceField<typename MaterialBlockType::T>
{
public:
    typedef core::behavior::ForceField<typename MaterialBlockType::T> Inherit;
    SOFA_ABSTRACT_CLASS(SOFA_TEMPLATE(BaseMaterialForceField,MaterialBlockType),SOFA_TEMPLATE(core::behavior::ForceField,typename MaterialBlockType::T));

    /** @name  Input types    */
    //@{
    typedef typename MaterialBlockType::T DataTypes;
    typedef typename DataTypes::Real Real;
    typedef typename DataTypes::Coord Coord;
    typedef typename DataTypes::Deriv Deriv;
    typedef typename DataTypes::VecCoord VecCoord;
    typedef typename DataTypes::VecDeriv VecDeriv;
    typedef Data<typename DataTypes::VecCoord> DataVecCoord;
    typedef Data<typename DataTypes::VecDeriv> DataVecDeriv;
    typedef core::behavior::MechanicalState<DataTypes> mstateType;
    //@}

    /** @name  material types    */
    //@{
    typedef MaterialBlockType BlockType;  ///< Material block object
    typedef vector<BlockType >  SparseMatrix;

    typedef typename BlockType::MatBlock  MatBlock;  ///< Material block matrix
    typedef linearsolver::EigenSparseMatrix<DataTypes,DataTypes>    SparseMatrixEigen;
    //@}


    /** @name forceField functions */
    //@{
    virtual void init()
    {
        if(!(this->mstate))
        {
            this->mstate = dynamic_cast<mstateType*>(this->getContext()->getMechanicalState());
            if(!(this->mstate)) { serr<<"state not found"<< sendl; return; }
        }

        // init material
        typename mstateType::ReadVecCoord X = this->mstate->readPositions();
        material.resize(X.size());

        // retrieve volume integrals
        engine::BaseGaussPointSampler* sampler=NULL;
        this->getContext()->get(sampler,core::objectmodel::BaseContext::SearchUp);
        if( !sampler ) { serr<<"Gauss point sampler not found -> use unit volumes"<< sendl; for(unsigned int i=0; i<material.size(); i++) material[i].volume=NULL; }
        else for(unsigned int i=0; i<material.size(); i++) material[i].volume=&sampler->f_volume.getValue()[i];

        reinit();

        Inherit::init();
    }

    virtual void reinit()
    {
        // reinit matrices
        if(this->assembleC.getValue()) updateC();
        if(this->assembleK.getValue()) updateK();
        if(this->assembleB.getValue()) updateB();

        Inherit::reinit();
    }

    virtual void addForce(const core::MechanicalParams* /*mparams*/ /* PARAMS FIRST */, DataVecDeriv& _f , const DataVecCoord& _x , const DataVecDeriv& _v)
    {
        if( isCompliance.getValue() ) return; // if seen as a compliance, then apply no force directly, they will be applied as constraints in writeConstraints

        VecDeriv&  f = *_f.beginEdit();
        const VecCoord&  x = _x.getValue();
        const VecDeriv&  v = _v.getValue();

        for(unsigned int i=0; i<material.size(); i++)
        {
            material[i].addForce(f[i],x[i],v[i]);
        }
        _f.endEdit();

        if(!BlockType::constantK)
        {
            if(this->assembleC.getValue()) updateC();
            if(this->assembleK.getValue()) updateK();
            if(this->assembleB.getValue()) updateB();
        }
    }

    virtual void addDForce(const core::MechanicalParams* mparams /* PARAMS FIRST */, DataVecDeriv&   _df , const DataVecDeriv&   _dx )
    {
        if( isCompliance.getValue() ) return; // if seen as a compliance, then apply no force directly, they will be applied as constraints

        if(this->assembleK.getValue())
        {
            K.addMult(_df,_dx,mparams->kFactor());
            if(this->assembleB.getValue())   B.addMult(_df,_dx,mparams->bFactor());
        }
        else
        {
            VecDeriv&  df = *_df.beginEdit();
            const VecDeriv&  dx = _dx.getValue();

            for(unsigned int i=0; i<material.size(); i++)
            {
                material[i].addDForce(df[i],dx[i],mparams->kFactor(),mparams->bFactor());
            }
            _df.endEdit();
        }
    }


    const defaulttype::BaseMatrix* getComplianceMatrix(const core::MechanicalParams */*mparams*/)
    {
        if( !isCompliance.getValue() ) return NULL; // if seen as a stiffness, then return no compliance matrix
        if(!this->assembleC.getValue()) updateC();
        return &C;
    }

    virtual const sofa::defaulttype::BaseMatrix* getStiffnessMatrix(const core::MechanicalParams*)
    {
        if( isCompliance.getValue() ) return NULL; // if seen as a compliance, then return no stiffness matrix
        if(!this->assembleK.getValue()) updateK();
//        cerr<<"BaseMaterialForceField::getStiffnessMatrix, K = " << K << endl;
        return &K;
    }

    const defaulttype::BaseMatrix* getB(const core::MechanicalParams */*mparams*/)
    {
        if(!this->assembleB.getValue()) updateB();
        return &B;
    }

    void draw(const core::visual::VisualParams* /*vparams*/)
    {
    }
    //@}



protected:

    BaseMaterialForceField(core::behavior::MechanicalState<DataTypes> *mm = NULL)
        : Inherit(mm)
        , assembleC ( initData ( &assembleC,false, "assembleC","Assemble the Compliance matrix" ) )
        , assembleK ( initData ( &assembleK,false, "assembleK","Assemble the Stiffness matrix" ) )
        , assembleB ( initData ( &assembleB,false, "assembleB","Assemble the Damping matrix" ) )
        , isCompliance( initData(&isCompliance, false, "isCompliance", "Consider the component as a compliance, else as a stiffness"))
    {

    }

    virtual ~BaseMaterialForceField()    {     }

    SparseMatrix material;

    Data<bool> assembleC;
    SparseMatrixEigen C;

    void updateC()
    {
        if(!(this->mstate)) { serr<<"state not found"<< sendl; return; }
        typename mstateType::ReadVecCoord X = this->mstate->readPositions();

        C.resizeBlocks(X.size(),X.size());
        for(unsigned int i=0; i<material.size(); i++)
        {
//            vector<MatBlock> blocks;
//            vector<unsigned> columns;
//            columns.push_back( i );
//            blocks.push_back( material[i].getC() );
//            C.appendBlockRow( i, columns, blocks );
            C.beginBlockRow(i);
            C.createBlock(i,material[i].getC());
            C.endBlockRow();
        }
//        C.endEdit();
        C.compress();
    }

    Data<bool> assembleK;
    SparseMatrixEigen K;

    void updateK()
    {
        if(!(this->mstate)) { serr<<"state not found"<< sendl; return; }
        typename mstateType::ReadVecCoord X = this->mstate->readPositions();

        K.resizeBlocks(X.size(),X.size());
        for(unsigned int i=0; i<material.size(); i++)
        {
//            vector<MatBlock> blocks;
//            vector<unsigned> columns;
//            columns.push_back( i );
//            blocks.push_back( material[i].getK() );
//            K.appendBlockRow( i, columns, blocks );
            K.beginBlockRow(i);
            K.createBlock(i,material[i].getK());
            K.endBlockRow();
        }
//        K.endEdit();
        K.compress();
    }


    Data<bool> assembleB;
    SparseMatrixEigen B;

    void updateB()
    {
        if(!(this->mstate)) { serr<<"state not found"<< sendl; return; }
        typename mstateType::ReadVecCoord X = this->mstate->readPositions();

        B.resizeBlocks(X.size(),X.size());
        for(unsigned int i=0; i<material.size(); i++)
        {
//            vector<MatBlock> blocks;
//            vector<unsigned> columns;
//            columns.push_back( i );
//            blocks.push_back( material[i].getB() );
//            B.appendBlockRow( i, columns, blocks );
            B.beginBlockRow(i);
            B.createBlock(i,material[i].getB());
            B.endBlockRow();
        }
//        B.endEdit();
        B.compress();
    }

    Data< bool > isCompliance;  ///< Consider as compliance, else consider as stiffness

};


}
}
}

#endif
