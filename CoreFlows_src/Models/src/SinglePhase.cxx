/*
 * SinglePhase.cxx
 *
 *  Created on: Sep 15, 2014
 *      Author: tn236279
 */

#include "SinglePhase.hxx"

using namespace std;

SinglePhase::SinglePhase(phaseType fluid, pressureEstimate pEstimate, int dim){
	_Ndim=dim;
	_nVar=_Ndim+2;
	_nbPhases  = 1;
	_dragCoeffs=vector<double>(1,0);
	_fluides.resize(1);
	if(pEstimate==around1bar300K){//EOS at 1 bar and 300K
		if(fluid==Gas){
			cout<<"Fluid is air around 1 bar and 300 K (27°C)"<<endl;
			_fluides[0] = new StiffenedGas(1.4,743,300,2.22e5);  //ideal gas law for nitrogen at pressure 1 bar and temperature 27°C, e=2.22e5, c_v=743
		}
		else{
			cout<<"Fluid is water around 1 bar and 300 K (27°C)"<<endl;
			_fluides[0] = new StiffenedGas(996,1e5,300,1.12e5,1501,4130);  //stiffened gas law for water at pressure 1 bar and temperature 27°C, e=1.12e5, c_v=4130
		}
	}
	else{//EOS at 155 bars and 618K 
		if(fluid==Gas){
			cout<<"Fluid is Gas around saturation point 155 bars and 618 K (345°C)"<<endl;
			_fluides[0] = new StiffenedGas(102,1.55e7,618,2.44e6, 433,3633);  //stiffened gas law for Gas at pressure 155 bar and temperature 345°C
		}
		else{//To do : change to normal regime: 155 bars and 573K
			cout<<"Fluid is water around saturation point 155 bars and 618 K (345°C)"<<endl;
			_fluides[0]= new StiffenedGas(594,1.55e7,618,1.6e6, 621,3100);  //stiffened gas law for water at pressure 155 bar, and temperature 345°C

			//*_fluides[0] = StiffenedGasDellacherie(2.35,1e9,-1.167e6,1816,618,1.6e6); //stiffened gas law for water from S. Dellacherie
		}
	}
}
void SinglePhase::initialize(){
	cout<<"Initialising the Navier-Stokes model"<<endl;

	_Uroe = new double[_nVar];
	_gravite = vector<double>(_nVar,0);
	for(int i=0; i<_Ndim; i++)
		_gravite[i+1]=_gravity3d[i];

	_Gravity = new PetscScalar[_nVar*_nVar];
	for(int i=0; i<_nVar*_nVar;i++)
		_Gravity[i] = 0;
	if(_timeScheme==Implicit)
	{
		for(int i=0; i<_nVar;i++)
			_Gravity[i*_nVar]=-_gravite[i];
	}
	if(_saveVelocity)
		_Vitesse=Field("Velocity",CELLS,_mesh,3);//Forcement en dimension 3 pour le posttraitement des lignes de courant

	if(_entropicCorrection)
		_entropicShift=vector<double>(3,0);//at most 3 distinct eigenvalues

	ProblemFluid::initialize();
}

void SinglePhase::convectionState( const long &i, const long &j, const bool &IsBord){

	_idm[0] = _nVar*i; 
	for(int k=1; k<_nVar; k++)
		_idm[k] = _idm[k-1] + 1;
	VecGetValues(_courant, _nVar, _idm, _Ui);

	_idm[0] = _nVar*j;
	for(int k=1; k<_nVar; k++)
		_idm[k] = _idm[k-1] + 1;
	if(IsBord)
		VecGetValues(_Uext, _nVar, _idm, _Uj);
	else
		VecGetValues(_courant, _nVar, _idm, _Uj);
	if(_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout<<"Convection Left state cell " << i<< ": "<<endl;
		for(int k =0; k<_nVar; k++)
			cout<< _Ui[k]<<endl;
		cout<<"Convection Right state cell " << j<< ": "<<endl;
		for(int k =0; k<_nVar; k++)
			cout<< _Uj[k]<<endl;
	}
	if(_Ui[0]<0||_Uj[0]<0)
	{
		cout<<"!!!!!!!!!!!!!!!!!!!!!!!!densite negative, arret de calcul!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"<<endl;
		throw CdmathException("densite negative, arret de calcul");
	}
	PetscScalar ri, rj, xi, xj, pi, pj;
	PetscInt Ii;
	ri = sqrt(_Ui[0]);//racine carre de phi_i rho_i
	rj = sqrt(_Uj[0]);
	_Uroe[0] = ri*rj;	//moyenne geometrique des densites
	if(_verbose && _nbTimeStep%_freqSave ==0)
		cout << "Densité moyenne Roe  gauche " << i << ": " << ri*ri << ", droite " << j << ": " << rj*rj << "->" << _Uroe[0] << endl;
	for(int k=0;k<_Ndim;k++){
		xi = _Ui[k+1];
		xj = _Uj[k+1];
		_Uroe[1+k] = (xi/ri + xj/rj)/(ri + rj);
		//"moyenne" des vitesses
		if(_verbose && _nbTimeStep%_freqSave ==0)
			cout << "Vitesse de Roe composante "<< k<<"  gauche " << i << ": " << xi/(ri*ri) << ", droite " << j << ": " << xj/(rj*rj) << "->" << _Uroe[k+1] << endl;
	}
	// H = (rho E + p)/rho
	xi = _Ui[_nVar-1];//phi rho E
	xj = _Uj[_nVar-1];
	Ii = i*_nVar; // correct Kieu
	VecGetValues(_primitives, 1, &Ii, &pi);// _primitives pour p
	if(IsBord)
	{
		double q_2 = 0;
		for(int k=1;k<=_Ndim;k++)
			q_2 += _Uj[k]*_Uj[k];
		q_2 /= _Uj[0];	//phi rho u²
		pj =  _fluides[0]->getPressure((_Uj[(_Ndim+2)-1]-q_2/2)/_porosityj,_Uj[0]/_porosityj);
	}
	else
	{
		Ii = j*_nVar; // correct Kieu
		VecGetValues(_primitives, 1, &Ii, &pj);
	}
	xi = (xi + pi)/(ri*ri);
	xj = (xj + pj)/(rj*rj);
	_Uroe[_nVar-1] = (ri*xi + rj*xj)/(ri + rj);
	//on se donne l enthalpie ici
	if(_verbose && _nbTimeStep%_freqSave ==0)
		cout << "Enthalpie totale de Roe H  gauche " << i << ": " << xi << ", droite " << j << ": " << xj << "->" << _Uroe[_nVar-1] << endl;
}

void SinglePhase::diffusionStateAndMatrices(const long &i,const long &j, const bool &IsBord){
	//sortie: matrices et etat de diffusion (rho, q, T)
	_idm[0] = _nVar*i;
	for(int k=1; k<_nVar; k++)
		_idm[k] = _idm[k-1] + 1;

	VecGetValues(_courant, _nVar, _idm, _Ui);
	_idm[0] = _nVar*j;
	for(int k=1; k<_nVar; k++)
		_idm[k] = _idm[k-1] + 1;

	if(IsBord)
		VecGetValues(_Uextdiff, _nVar, _idm, _Uj);
	else
		VecGetValues(_courant, _nVar, _idm, _Uj);

	if(_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout << "SinglePhase::diffusionStateAndMatrices cellule gauche" << i << endl;
		cout << "Ui = ";
		for(int q=0; q<_nVar; q++)
			cout << _Ui[q]  << "\t";
		cout << endl;
		cout << "SinglePhase::diffusionStateAndMatrices cellule droite" << j << endl;
		cout << "Uj = ";
		for(int q=0; q<_nVar; q++)
			cout << _Uj[q]  << "\t";
		cout << endl;
	}

	for(int k=0; k<_nVar; k++)
		_Udiff[k] = (_Ui[k]/_porosityi+_Uj[k]/_porosityj)/2;

	if(_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout << "SinglePhase::diffusionStateAndMatrices conservative diffusion state" << endl;
		cout << "_Udiff = ";
		for(int q=0; q<_nVar; q++)
			cout << _Udiff[q]  << "\t";
		cout << endl;
		cout << "porosite gauche= "<<_porosityi<< ", porosite droite= "<<_porosityj<<endl;
	}
	consToPrim(_Udiff,_phi,1);
	_Udiff[_nVar-1]=_phi[_nVar-1];

	if(_timeScheme==Implicit)
	{
		double q_2=0;
		for (int i = 0; i<_Ndim;i++)
			q_2+=_Udiff[i+1]*_Udiff[i+1];
		double mu = _fluides[0]->getViscosity(_Udiff[_nVar-1]);
		double lambda = _fluides[0]->getConductivity(_Udiff[_nVar-1]);
		double Cv= _fluides[0]->constante("Cv");
		for(int i=0; i<_nVar*_nVar;i++)
			_Diffusion[i] = 0;
		for(int i=1;i<(_nVar-1);i++)
		{
			_Diffusion[i*_nVar] =  mu*_Udiff[i]/(_Udiff[0]*_Udiff[0]);
			_Diffusion[i*_nVar+i] = -mu/_Udiff[0];
		}
		int i = (_nVar-1)*_nVar;
		_Diffusion[i]=lambda*(_Udiff[_nVar-1]/_Udiff[0]-q_2/(2*Cv*_Udiff[0]*_Udiff[0]*_Udiff[0]));
		for(int k=1;k<(_nVar-1);k++)
		{
			_Diffusion[i+k]= lambda*_Udiff[k]/(_Udiff[0]*_Udiff[0]*Cv);
		}
		_Diffusion[_nVar*_nVar-1]=-lambda/(_Udiff[0]*Cv);
	}

}
void SinglePhase::setBoundaryState(string nameOfGroup, const int &j,double *normale){
	int k;
	double v2=0, q_n=0;//quantité de mouvement normale à la face frontière;
	_idm[0] = _nVar*j;
	for(k=1; k<_nVar; k++)
		_idm[k] = _idm[k-1] + 1;

	VecGetValues(_courant, _nVar, _idm, _externalStates);
	for(k=0; k<_Ndim; k++)
		q_n+=_externalStates[(k+1)]*normale[k];

	if(_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout << "Boundary conditions for group "<< nameOfGroup << " face unit normal vector "<<endl;
		for(k=0; k<_Ndim; k++){
			cout<<normale[k]<<", ";
		}
		cout<<endl;
	}

	if (_limitField[nameOfGroup].bcType==Wall){
		for(k=0; k<_Ndim; k++)
			_externalStates[(k+1)]-= 2*q_n*normale[k];

		_idm[0] = 0;
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;

		VecAssemblyBegin(_Uext);
		VecSetValues(_Uext, _nVar, _idm, _externalStates, INSERT_VALUES);
		VecAssemblyEnd(_Uext);

		_idm[0] = _nVar*j;
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;
		VecGetValues(_primitives, _nVar, _idm, _externalStates);
		double pression=_externalStates[0];
		double T=_limitField[nameOfGroup].T;
		double rho=_fluides[0]->getDensity(pression,T);
		_externalStates[0]=rho;
		_externalStates[1]=_externalStates[0]*_limitField[nameOfGroup].v_x[0];
		v2 +=_limitField[nameOfGroup].v_x[0]*_limitField[nameOfGroup].v_x[0];
		if(_Ndim>1)
		{
			v2 +=_limitField[nameOfGroup].v_y[0]*_limitField[nameOfGroup].v_y[0];
			_externalStates[2]=_externalStates[0]*_limitField[nameOfGroup].v_y[0];
			if(_Ndim==3)
			{
				_externalStates[3]=_externalStates[0]*_limitField[nameOfGroup].v_z[0];
				v2 +=_limitField[nameOfGroup].v_z[0]*_limitField[nameOfGroup].v_z[0];
			}
		}
		_externalStates[_nVar-1] = _externalStates[0]*(_fluides[0]->getInternalEnergy(_limitField[nameOfGroup].T,rho) + v2/2);
		_idm[0] = 0;
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;
		VecAssemblyBegin(_Uextdiff);
		VecSetValues(_Uextdiff, _nVar, _idm, _externalStates, INSERT_VALUES);
		VecAssemblyEnd(_Uextdiff);
	}
	else if (_limitField[nameOfGroup].bcType==Neumann){
		_idm[0] = 0;
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;

		VecAssemblyBegin(_Uext);
		VecSetValues(_Uext, _nVar, _idm, _externalStates, INSERT_VALUES);
		VecAssemblyEnd(_Uext);

		VecAssemblyBegin(_Uextdiff);
		VecSetValues(_Uextdiff, _nVar, _idm, _externalStates, INSERT_VALUES);
		VecAssemblyEnd(_Uextdiff);
	}
	else if (_limitField[nameOfGroup].bcType==Inlet){

		if(q_n<=0){
			VecGetValues(_primitives, _nVar, _idm, _externalStates);
			double pression=_externalStates[0];
			double T=_limitField[nameOfGroup].T;
			double rho=_fluides[0]->getDensity(pression,T);
			_externalStates[0]=rho;
			_externalStates[1]=_externalStates[0]*(_limitField[nameOfGroup].v_x[0]);
			v2 +=(_limitField[nameOfGroup].v_x[0])*(_limitField[nameOfGroup].v_x[0]);
			if(_Ndim>1)
			{
				v2 +=_limitField[nameOfGroup].v_y[0]*_limitField[nameOfGroup].v_y[0];
				_externalStates[2]=_externalStates[0]*_limitField[nameOfGroup].v_y[0];
				if(_Ndim==3)
				{
					_externalStates[3]=_externalStates[0]*_limitField[nameOfGroup].v_z[0];
					v2 +=_limitField[nameOfGroup].v_z[0]*_limitField[nameOfGroup].v_z[0];
				}
			}
			_externalStates[_nVar-1] = _externalStates[0]*(_fluides[0]->getInternalEnergy(_limitField[nameOfGroup].T,rho) + v2/2);
		}
		else if(_nbTimeStep%_freqSave ==0)
			cout<< "Warning : fluid going out through inlet boundary "<<nameOfGroup<<". Applying Neumann boundary condition"<<endl;

		_idm[0] = 0;
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;
		VecAssemblyBegin(_Uext);
		VecAssemblyBegin(_Uextdiff);
		VecSetValues(_Uext, _nVar, _idm, _externalStates, INSERT_VALUES);
		VecSetValues(_Uextdiff, _nVar, _idm, _externalStates, INSERT_VALUES);
		VecAssemblyEnd(_Uext);
		VecAssemblyEnd(_Uextdiff);
	}
	else if (_limitField[nameOfGroup].bcType==InletPressure){

		VecGetValues(_primitives, _nVar, _idm, _externalStates);
		if(q_n<=0){
			_externalStates[0]=_fluides[0]->getDensity(_limitField[nameOfGroup].p,_limitField[nameOfGroup].T);
		}
		else{
			if(_nbTimeStep%_freqSave ==0)
				cout<< "Warning : fluid going out through inletPressure boundary "<<nameOfGroup<<". Applying Neumann boundary condition for velocity and temperature"<<endl;
			_externalStates[0]=_fluides[0]->getDensity(_limitField[nameOfGroup].p, _externalStates[_nVar-1]);
		}

		for(k=0; k<_Ndim; k++)
		{
			v2+=_externalStates[(k+1)]*_externalStates[(k+1)];
			_externalStates[(k+1)]*=_externalStates[0] ;
		}
		_externalStates[_nVar-1] = _externalStates[0]*(_fluides[0]->getInternalEnergy( _externalStates[_nVar-1],_externalStates[0]) + v2/2);


		_idm[0] = 0;
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;
		VecAssemblyBegin(_Uext);
		VecAssemblyBegin(_Uextdiff);
		VecSetValues(_Uext, _nVar, _idm, _externalStates, INSERT_VALUES);
		VecSetValues(_Uextdiff, _nVar, _idm, _externalStates, INSERT_VALUES);
		VecAssemblyEnd(_Uext);
		VecAssemblyEnd(_Uextdiff);
	}
	else if (_limitField[nameOfGroup].bcType==Outlet){
		if(q_n<=0 &&  _nbTimeStep%_freqSave ==0)
			cout<< "Warning : fluid going in through outlet boundary "<<nameOfGroup<<". Applying Neumann boundary condition for velocity and temperature"<<endl;

		_idm[0] = _nVar*j;// Kieu
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;
		VecGetValues(_primitives, _nVar, _idm, _externalStates);

		_externalStates[0]=_fluides[0]->getDensity(_limitField[nameOfGroup].p, _externalStates[_nVar-1]);
		for(k=0; k<_Ndim; k++)
		{
			v2+=_externalStates[(k+1)]*_externalStates[(k+1)];
			//mif (k==1) cout<< " vitesse sortie conv x= "<< _externalStates[1]<<endl;
			_externalStates[(k+1)]*=_externalStates[0] ;
		}
		_externalStates[_nVar-1] = _externalStates[0]*(_fluides[0]->getInternalEnergy( _externalStates[_nVar-1],_externalStates[0]) + v2/2);
		_idm[0] = 0;
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;
		VecAssemblyBegin(_Uext);
		VecAssemblyBegin(_Uextdiff);
		VecSetValues(_Uext, _nVar, _idm, _externalStates, INSERT_VALUES);
		VecSetValues(_Uextdiff, _nVar, _idm, _externalStates, INSERT_VALUES);
		VecAssemblyEnd(_Uext);
		VecAssemblyEnd(_Uextdiff);
	}else {
		cout<<"Boundary condition not set for boundary named"<<nameOfGroup<<endl;
		cout<<"Accepted boundary condition are Neumann, Wall, Inlet, and Outlet"<<endl;
		throw CdmathException("Unknown boundary condition");
	}
}

void SinglePhase::convectionMatrices()
{
	//entree: URoe = rho, u, H
	//sortie: matrices Roe+  et Roe-

	double u_n=0, u_2=0;//vitesse normale et carré du module

	for(int i=0;i<_Ndim;i++)
	{
		u_2 += _Uroe[1+i]*_Uroe[1+i];
		u_n += _Uroe[1+i]*_vec_normal[i];
	}

	vector<complex<double> > vp_dist(3,0);

	if(_spaceScheme==staggered && _nonLinearFormulation==VFFC)//special case
		staggeredVFFCMatrices(u_n);
	else
	{
		double  c, H, K, k;

		/***********Calcul des valeurs propres ********/
		H = _Uroe[_nVar-1];
		c = _fluides[0]->vitesseSonEnthalpie(H-u_2/2);//vitesse du son a l'interface
		k = _fluides[0]->constante("gamma") - 1;//A generaliser pour porosite et stephane gas law
		K = u_2*k/2; //g-1/2 *|u|²

		vp_dist[0]=u_n-c;vp_dist[1]=u_n;vp_dist[2]=u_n+c;

		_maxvploc=fabs(u_n)+c;
		if(_maxvploc>_maxvp)
			_maxvp=_maxvploc;

		if(_verbose && _nbTimeStep%_freqSave ==0)
			cout<<"Valeurs propres "<<u_n-c<<" , "<<u_n<<" , "<<u_n+c<<endl;

		/******** Construction de la matrice de Roe *********/
		//premiere ligne (masse)
		_Aroe[0]=0;
		for(int idim=0; idim<_Ndim;idim++)
			_Aroe[1+idim]=_vec_normal[idim];
		_Aroe[_nVar-1]=0;

		//lignes intermadiaires(qdm)
		for(int idim=0; idim<_Ndim;idim++)
		{
			//premiere colonne
			_Aroe[(1+idim)*_nVar]=K*_vec_normal[idim] - u_n*_Uroe[1+idim];
			//colonnes intermediaires
			for(int jdim=0; jdim<_Ndim;jdim++)
				_Aroe[(1+idim)*_nVar + jdim + 1] = _Uroe[1+idim]*_vec_normal[jdim]-k*_vec_normal[idim]*_Uroe[1+jdim];
			//matrice identite
			_Aroe[(1+idim)*_nVar + idim + 1] += u_n;
			//derniere colonne
			_Aroe[(1+idim)*_nVar + _nVar-1]=k*_vec_normal[idim];
		}

		//derniere ligne (energie)
		_Aroe[_nVar*(_nVar-1)] = (K - H)*u_n;
		for(int idim=0; idim<_Ndim;idim++)
			_Aroe[_nVar*(_nVar-1)+idim+1]=H*_vec_normal[idim] - k*u_n*_Uroe[idim+1];
		_Aroe[_nVar*_nVar -1] = (1 + k)*u_n;

		/******** Construction des matrices de decentrement ********/
		if( _spaceScheme ==centered){
			if(_entropicCorrection)
				throw CdmathException("SinglePhase::roeMatrices: entropy scheme not available for centered scheme");

			for(int i=0; i<_nVar*_nVar;i++)
				_absAroe[i] = 0;
		}
		else if(_spaceScheme == upwind || _spaceScheme ==pressureCorrection || _spaceScheme ==lowMach || (_spaceScheme==staggered )){
			if(_entropicCorrection)
				entropicShift(_vec_normal);
			else
				_entropicShift=vector<double>(3,0);//at most 3 distinct eigenvalues

			vector< complex< double > > y (3,0);
			Polynoms Poly;
			for( int i=0 ; i<3 ; i++)
				y[i] = Poly.abs_generalise(vp_dist[i])+_entropicShift[i];
			Poly.abs_par_interp_directe(3,vp_dist, _Aroe, _nVar,_precision, _absAroe,y);

			if( _spaceScheme ==pressureCorrection)
				for( int i=0 ; i<_Ndim ; i++)
					for( int j=0 ; j<_Ndim ; j++)
						_absAroe[(1+i)*_nVar+1+j]-=(vp_dist[2].real()-vp_dist[0].real())/2*_vec_normal[i]*_vec_normal[j];
			else if( _spaceScheme ==lowMach){
				double M=sqrt(u_2)/c;
				for( int i=0 ; i<_Ndim ; i++)
					for( int j=0 ; j<_Ndim ; j++)
						_absAroe[(1+i)*_nVar+1+j]-=(1-M)*(vp_dist[2].real()-vp_dist[0].real())/2*_vec_normal[i]*_vec_normal[j];
			}
		}
		else if( _spaceScheme ==staggered ){
			if(_entropicCorrection)//To do: study entropic correction for staggered
				throw CdmathException("SinglePhase::roeMatrices: entropy scheme not available for staggered scheme");
			//Calcul de décentrement de type décalé
			//premiere ligne (masse)
			_absAroe[0]=0;
			for(int idim=0; idim<_Ndim;idim++)
				_absAroe[1+idim]=_vec_normal[idim];
			_absAroe[_nVar-1]=0;

			//lignes intermadiaires(qdm)
			for(int idim=0; idim<_Ndim;idim++)
			{
				//premiere colonne
				_absAroe[(1+idim)*_nVar]=-K*_vec_normal[idim] - u_n*_Uroe[1+idim];
				//colonnes intermediaires
				for(int jdim=0; jdim<_Ndim;jdim++)
					_absAroe[(1+idim)*_nVar + jdim + 1] = _Uroe[1+idim]*_vec_normal[jdim]+k*_vec_normal[idim]*_Uroe[1+jdim];
				//matrice identite
				_absAroe[(1+idim)*_nVar + idim + 1] += u_n;
				//derniere colonne
				_absAroe[(1+idim)*_nVar + _nVar-1]=-k*_vec_normal[idim];
			}

			//derniere ligne (energie)
			_absAroe[_nVar*(_nVar-1)] = (-K - H)*u_n;
			for(int idim=0; idim<_Ndim;idim++)
				_absAroe[_nVar*(_nVar-1)+idim+1]=H*_vec_normal[idim] + k*u_n*_Uroe[idim+1];
			_absAroe[_nVar*_nVar -1] = (1 - k)*u_n;

			double signu;
			if(u_n>0)
				signu=1;
			else if (u_n<0)
				signu=-1;
			else
				signu=0;

			for(int i=0; i<_nVar*_nVar;i++)
				_absAroe[i] *= signu;
		}
		else
			throw CdmathException("SinglePhase::roeMatrices: scheme not treated");

		if(_verbose && _nbTimeStep%_freqSave ==0)
		{
			cout<<endl<<"Matrice de Roe"<<endl;
			for(int i=0; i<_nVar;i++)
			{
				for(int j=0; j<_nVar;j++)
					cout << _Aroe[i*_nVar+j]<< " , ";
				cout<<endl;
			}
			cout<<"Valeur absolue matrice de Roe"<<endl;
			for(int i=0; i<_nVar;i++){
				for(int j=0; j<_nVar;j++)
					cout<<_absAroe[i*_nVar+j]<<" , ";
				cout<<endl;
			}
		}
		for(int i=0; i<_nVar*_nVar;i++)
		{
			_AroeMinus[i] = (_Aroe[i]-_absAroe[i])/2;
			_AroePlus[i]  = (_Aroe[i]+_absAroe[i])/2;
		}
	}

	/*********Calcul de la matrice signe pour VFFC, VFRoe et décentrement des termes source*****/
	if(_entropicCorrection || (_spaceScheme ==pressureCorrection ))
	{
		InvMatriceRoe( vp_dist);
		Polynoms Poly;
		Poly.matrixProduct(_absAroe, _nVar, _nVar, _invAroe, _nVar, _nVar, _signAroe);
	}
	else if (_spaceScheme==upwind || (_spaceScheme ==lowMach ))//upwind sans entropic
		SigneMatriceRoe( vp_dist);
	else if (_spaceScheme==centered)//centre  sans entropic
		for(int i=0; i<_nVar*_nVar;i++)
			_signAroe[i] = 0;
	else if( _spaceScheme ==staggered )//à tester
	{
		double signu;
		if(u_n>0)
			signu=1;
		else if (u_n<0)
			signu=-1;
		else
			signu=0;
		for(int i=0; i<_nVar*_nVar;i++)
			_signAroe[i] = 0;
		_signAroe[0] = signu;
		for(int i=1; i<_nVar-1;i++)
			_signAroe[i*_nVar+i] = -signu;
		_signAroe[_nVar*(_nVar-1)+_nVar-1] = signu;
	}
	else
		throw CdmathException("SinglePhase::roeMatrices: well balanced option not treated");

	if(_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout<<endl<<"Matrice _AroeMinus"<<endl;
		for(int i=0; i<_nVar;i++)
		{
			for(int j=0; j<_nVar;j++)
				cout << _AroeMinus[i*_nVar+j]<< " , ";
			cout<<endl;
		}
		cout<<endl<<"Matrice _AroePlus"<<endl;
		for(int i=0; i<_nVar;i++)
		{
			for(int j=0; j<_nVar;j++)
				cout << _AroePlus[i*_nVar+j]<< " , ";
			cout<<endl;
		}
	}
}
void SinglePhase::computeScaling(double maxvp)
{
	_blockDiag[0]=1;
	_invBlockDiag[0]=1;
	for(int q=1; q<_nVar-1; q++)
	{
		_blockDiag[q]=1./maxvp;//
		_invBlockDiag[q]= maxvp;//1.;//
	}
	_blockDiag[_nVar - 1]=(_fluides[0]->constante("gamma")-1)/(maxvp*maxvp);//1
	_invBlockDiag[_nVar - 1]=  1./_blockDiag[_nVar - 1] ;// 1.;//
}

void SinglePhase::addDiffusionToSecondMember
(		const int &i,
		const int &j,
		bool isBord)

{
	double lambda=_fluides[0]->getConductivity(_Udiff[_nVar-1]);
	double mu = _fluides[0]->getViscosity(_Udiff[_nVar-1]);

	if(lambda==0 && mu ==0 && _heatTransfertCoeff==0)
		return;

	//extraction des valeurs
	_idm[0] = _nVar*i; // Kieu
	for(int k=1; k<_nVar; k++)
		_idm[k] = _idm[k-1] + 1;

	VecGetValues(_primitives, _nVar, _idm, _Vi);
	if (_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout << "Calcul diffusion: variables primitives maille " << i<<endl;
		for(int q=0; q<_nVar; q++)
			cout << _Vi[q] << endl;
		cout << endl;
	}

	if(!isBord ){
		for(int k=0; k<_nVar; k++)
			_idn[k] = _nVar*j + k;

		VecGetValues(_primitives, _nVar, _idn, _Vj);
	}
	else
	{
		lambda=max(lambda,_heatTransfertCoeff);//wall nucleate boing -> larger heat transfer
		for(int k=0; k<_nVar; k++)
			_idn[k] = k;

		VecGetValues(_Uextdiff, _nVar, _idn, _phi);
		consToPrim(_phi,_Vj,1);
	}

	if (_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout << "Calcul diffusion: variables primitives maille " <<j <<endl;
		for(int q=0; q<_nVar; q++)
			cout << _Vj[q] << endl;
		cout << endl;
	}
	//on n'a pas de contribution sur la masse
	_phi[0]=0;
	//contribution visqueuse sur la quantite de mouvement
	for(int k=1; k<_nVar-1; k++)
		_phi[k] = _inv_dxi*2/(1/_inv_dxi+1/_inv_dxj)*mu*(_porosityj*_Vj[k] - _porosityi*_Vi[k]);

	//contribution visqueuse sur l'energie
	_phi[_nVar-1] = _inv_dxi*2/(1/_inv_dxi+1/_inv_dxj)*lambda*(_porosityj*_Vj[_nVar-1] - _porosityi*_Vi[_nVar-1]);

	_idm[0] = i;
	VecSetValuesBlocked(_b, 1, _idm, _phi, ADD_VALUES);

	if(_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout << "Contribution diffusion au 2nd membre pour la maille " << i << ": "<<endl;
		for(int q=0; q<_nVar; q++)
			cout << _phi[q] << endl;
		cout << endl;
	}

	if(!isBord)
	{
		//On change de signe pour l'autre contribution
		for(int k=0; k<_nVar; k++)
			_phi[k] *= -_inv_dxj/_inv_dxi;
		_idn[0] = j;

		VecSetValuesBlocked(_b, 1, _idn, _phi, ADD_VALUES);
		if(_verbose && _nbTimeStep%_freqSave ==0)
		{
			cout << "Contribution diffusion au 2nd membre pour la maille  " << j << ": "<<endl;
			for(int q=0; q<_nVar; q++)
				cout << _phi[q] << endl;
			cout << endl;
		}
	}

	if(_verbose && _nbTimeStep%_freqSave ==0 && _timeScheme==Implicit)
	{
		cout << "Matrice de diffusion D, pour le couple (" << i << "," << j<< "):" << endl;
		for(int i=0; i<_nVar; i++)
		{
			for(int j=0; j<_nVar; j++)
				cout << _Diffusion[i*_nVar+j]<<", ";
			cout << endl;
		}
		cout << endl;
	}
}

void SinglePhase::sourceVector(PetscScalar * Si, PetscScalar * Ui, PetscScalar * Vi, int i)
{
	double phirho=Ui[0], T=Vi[_nVar-1];
	double norm_u=0;
	for(int k=0; k<_Ndim; k++)
		norm_u+=Vi[1+k]*Vi[1+k];
	norm_u=sqrt(norm_u);
	if(T>_Tsat)
		Si[0]=_heatPowerField(i)/_latentHeat;
	else
		Si[0]=0;
	for(int k=1; k<_nVar-1; k++)
		Si[k]  =(_gravite[k]-_dragCoeffs[0]*norm_u*Vi[1+k])*phirho;

	Si[_nVar-1]=_heatPowerField(i);

	for(int k=0; k<_Ndim; k++)
		Si[_nVar-1] +=(_gravity3d[k]-_dragCoeffs[0]*norm_u*Vi[1+k])*Vi[1+k]*phirho;

	if(_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout<<"SinglePhase::sourceVector"<<endl;
		cout<<"Ui="<<endl;
		for(int k=0;k<_nVar;k++)
			cout<<Ui[k]<<", ";
		cout<<endl;
		cout<<"Vi="<<endl;
		for(int k=0;k<_nVar;k++)
			cout<<Vi[k]<<", ";
		cout<<endl;
		cout<<"Si="<<endl;
		for(int k=0;k<_nVar;k++)
			cout<<Si[k]<<", ";
		cout<<endl;
	}
}
void SinglePhase::pressureLossVector(PetscScalar * pressureLoss, double K, PetscScalar * Ui, PetscScalar * Vi, PetscScalar * Uj, PetscScalar * Vj)
{
	double norm_u=0, u_n=0, rho;
	for(int i=0;i<_Ndim;i++)
		u_n += _Uroe[1+i]*_vec_normal[i];

	pressureLoss[0]=0;
	if(u_n>0){
		for(int i=0;i<_Ndim;i++)
			norm_u += Vi[1+i]*Vi[1+i];
		norm_u=sqrt(norm_u);
		rho=Ui[0];
		for(int i=0;i<_Ndim;i++)
			pressureLoss[1+i]=-1/2*K*rho*norm_u*Vi[1+i];
	}
	else{
		for(int i=0;i<_Ndim;i++)
			norm_u += Vj[1+i]*Vj[1+i];
		norm_u=sqrt(norm_u);
		rho=Uj[0];
		for(int i=0;i<_Ndim;i++)
			pressureLoss[1+i]=-1/2*K*rho*norm_u*Vj[1+i];
	}
	pressureLoss[_nVar-1]=-1/2*K*rho*norm_u*norm_u*norm_u;

	if(_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout<<"SinglePhase::pressureLossVector K= "<<K<<endl;
		cout<<"Ui="<<endl;
		for(int k=0;k<_nVar;k++)
			cout<<Ui[k]<<", ";
		cout<<endl;
		cout<<"Vi="<<endl;
		for(int k=0;k<_nVar;k++)
			cout<<Vi[k]<<", ";
		cout<<endl;
		cout<<"Uj="<<endl;
		for(int k=0;k<_nVar;k++)
			cout<<Uj[k]<<", ";
		cout<<endl;
		cout<<"Vj="<<endl;
		for(int k=0;k<_nVar;k++)
			cout<<Vj[k]<<", ";
		cout<<endl;
		cout<<"pressureLoss="<<endl;
		for(int k=0;k<_nVar;k++)
			cout<<pressureLoss[k]<<", ";
		cout<<endl;
	}
}

void SinglePhase::porosityGradientSourceVector()
{
	double u_ni=0, u_nj=0, rhoi,rhoj, pi=_Vi[0], pj=_Vj[0],pij;
	for(int i=0;i<_Ndim;i++) {
		u_ni += _Vi[1+i]*_vec_normal[i];
		u_nj += _Vj[1+i]*_vec_normal[i];
	}
	_porosityGradientSourceVector[0]=0;
	rhoj=_Uj[0]/_porosityj;
	rhoi=_Ui[0]/_porosityi;
	pij=(pi+pj)/2+rhoi*rhoj/2/(rhoi+rhoj)*(u_ni-u_nj)*(u_ni-u_nj);
	for(int i=0;i<_Ndim;i++)
		_porosityGradientSourceVector[1+i]=pij*(_porosityi-_porosityj)*2/(1/_inv_dxi+1/_inv_dxj);
	_porosityGradientSourceVector[_nVar-1]=0;
}

void SinglePhase::jacobian(const int &j, string nameOfGroup,double * normale)
{
	int k;
	for(k=0; k<_nVar*_nVar;k++)
		_Jcb[k] = 0;

	_idm[0] = _nVar*j;
	for(k=1; k<_nVar; k++)
		_idm[k] = _idm[k-1] + 1;
	VecGetValues(_courant, _nVar, _idm, _externalStates);
	double q_n=0;//quantité de mouvement normale à la paroi
	for(k=0; k<_Ndim; k++)
		q_n+=_externalStates[(k+1)]*normale[k];

	// loop of boundary types
	if (_limitField[nameOfGroup].bcType==Wall)
	{
		for(k=0; k<_nVar;k++)
			_Jcb[k*_nVar + k] = 1;
		for(k=1; k<_nVar-1;k++)
			for(int l=1; l<_nVar-1;l++)
				_Jcb[k*_nVar + l] -= 2*normale[k-1]*normale[l-1];
	}
	else if (_limitField[nameOfGroup].bcType==Inlet)
	{
		if(q_n<0){
			double v[_Ndim], ve[_Ndim], v2, ve2;

			_idm[0] = _nVar*j;
			for(k=1; k<_nVar; k++)
				_idm[k] = _idm[k-1] + 1;
			VecGetValues(_primitives, _nVar, _idm, _Vj);
			VecGetValues(_courant, _nVar, _idm, _Uj);

			ve[0] = _limitField[nameOfGroup].v_x[0];
			v[0]=_Vj[1];
			ve2 = ve[0]*ve[0];
			v2 = v[0]*v[0];
			if (_Ndim >1){
				ve[1] = _limitField[nameOfGroup].v_y[0];
				v[1]=_Vj[2];
				ve2 += ve[1]*ve[1];
				v2 = v[1]*v[1];
			}
			if (_Ndim >2){
				ve[2] = _limitField[nameOfGroup].v_z[0];
				v[2]=_Vj[3];
				ve2 += ve[2]*ve[2];
				v2 = v[2]*v[2];
			}
			double internal_energy=_fluides[0]->getInternalEnergy(_limitField[nameOfGroup].T,_Uj[0]);
			double total_energy=internal_energy+ve2/2;

			//Mass line
			_Jcb[0]=v2/(2*internal_energy);
			for(k=0; k<_Ndim;k++)
				_Jcb[1+k]=-v[k]/internal_energy;
			_Jcb[_nVar-1]=1/internal_energy;
			//Momentum lines
			for(int l =1;l<1+_Ndim;l++){
				_Jcb[l*_nVar]=v2*ve[l-1]/(2*internal_energy);
				for(k=0; k<_Ndim;k++)
					_Jcb[l*_nVar+1+k]=-v[k]*ve[l-1]/internal_energy;
				_Jcb[l*_nVar+_nVar-1]=ve[l-1]/internal_energy;
			}
			//Energy line
			_Jcb[(_nVar-1)*_nVar]=v2*total_energy/(2*internal_energy);
			for(k=0; k<_Ndim;k++)
				_Jcb[(_nVar-1)*_nVar+1+k]=-v[k]*total_energy/internal_energy;
			_Jcb[(_nVar-1)*_nVar+_nVar-1]=total_energy/internal_energy;
		}
		else
			for(k=0;k<_nVar;k++)
				_Jcb[k*_nVar+k]=1;
		//Old jacobian
		/*
		 _Jcb[0] = 1;
		_Jcb[_nVar]=_limitField[nameOfGroup].v_x[0];//Kieu
		v2 +=(_limitField[nameOfGroup].v_x[0])*(_limitField[nameOfGroup].v_x[0]);
		if(_Ndim>1)
		{
			_Jcb[2*_nVar]= _limitField[nameOfGroup].v_y[0];
			v2 +=_limitField[nameOfGroup].v_y[0]*_limitField[nameOfGroup].v_y[0];
			if(_Ndim==3){
				_Jcb[3*_nVar]=_limitField[nameOfGroup].v_z[0];
				v2 +=_limitField[nameOfGroup].v_z[0]*_limitField[nameOfGroup].v_z[0];
			}
		}
		_Jcb[(_nVar-1)*_nVar]=_fluides[0]->getInternalEnergy(_limitField[nameOfGroup].T,rho) + v2/2;
		 */
	}
	else if (_limitField[nameOfGroup].bcType==InletPressure && q_n<0){
		double v[_Ndim], v2=0;
		_idm[0] = _nVar*j;
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;
		VecGetValues(_primitives, _nVar, _idm, _Vj);

		for(k=0; k<_Ndim;k++){
			v[k]=_Vj[1+k];
			v2+=v[k]*v[k];
		}

		double rho_ext=_fluides[0]->getDensity(_limitField[nameOfGroup].p, _limitField[nameOfGroup].T);
		double rho_int = _externalStates[0];
		double density_ratio=rho_ext/rho_int;
		//Momentum lines
		for(int l =1;l<1+_Ndim;l++){
			_Jcb[l*_nVar]=-density_ratio*v[l-1];
			_Jcb[l*_nVar+l]=density_ratio;
		}
		//Energy lines
		_Jcb[(_nVar-1)*_nVar]=-v2*density_ratio;
		for(k=0; k<_Ndim;k++)
			_Jcb[(_nVar-1)*_nVar+1+k]=density_ratio*v[k];
	}
	// not wall, not inlet, not inletPressure
	else if(_limitField[nameOfGroup].bcType==Outlet || (_limitField[nameOfGroup].bcType==InletPressure && q_n>=0))
	{
		double v[_Ndim], v2=0;
		_idm[0] = _nVar*j;
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;
		VecGetValues(_primitives, _nVar, _idm, _Vj);

		for(k=0; k<_Ndim;k++){
			v[k]=_Vj[1+k];
			v2+=v[k]*v[k];
		}

		double rho_ext=_fluides[0]->getDensity(_limitField[nameOfGroup].p, _externalStates[_nVar-1]);
		double rho_int = _externalStates[0];
		double density_ratio=rho_ext/rho_int;
		double internal_energy=_fluides[0]->getInternalEnergy(_externalStates[_nVar-1],rho_int);
		double total_energy=internal_energy+v2/2;

		//Mass line
		_Jcb[0]=density_ratio*(1-v2/(2*internal_energy));
		for(k=0; k<_Ndim;k++)
			_Jcb[1+k]=density_ratio*v[k]/internal_energy;
		_Jcb[_nVar-1]=-density_ratio/internal_energy;
		//Momentum lines
		for(int l =1;l<1+_Ndim;l++){
			_Jcb[l*_nVar]=density_ratio*v2*v[l-1]/(2*internal_energy);
			for(k=0; k<_Ndim;k++)
				_Jcb[l*_nVar+1+k]=density_ratio*v[k]*v[l-1]/internal_energy;
			_Jcb[l*_nVar+1+k]-=density_ratio;
			_Jcb[l*_nVar+_nVar-1]=-density_ratio*v[l-1]/internal_energy;
		}
		//Energy line
		_Jcb[(_nVar-1)*_nVar]=density_ratio*v2*total_energy/(2*internal_energy);
		for(k=0; k<_Ndim;k++)
			_Jcb[(_nVar-1)*_nVar+1+k]=density_ratio*v[k]*total_energy/internal_energy;
		_Jcb[(_nVar-1)*_nVar+_nVar-1]=density_ratio*(1-total_energy/internal_energy);
		//Old jacobian
		/*
		int idim,jdim;
		double cd = 1,cn=0,p0, gamma;
		_idm[0] = j*_nVar;// Kieu
		for(k=1; k<_nVar;k++)
			_idm[k] = _idm[k-1] + 1;
		VecGetValues(_courant, _nVar, _idm, _phi);
		VecGetValues(_primitives, _nVar, _idm, _externalStates);

		// compute the common numerator and common denominator
		p0=_fluides[0]->constante("p0");
		gamma =_fluides[0]->constante("gamma");
		cn =_limitField[nameOfGroup].p +p0;
		cd = _phi[0]*_fluides[0]->getInternalEnergy(_externalStates[_nVar-1],rho)-p0;
		cd*=cd;
		cd*=(gamma-1);
		//compute the v2
		for(k=1; k<_nVar-1;k++)
			v2+=_externalStates[k]*_externalStates[k];
		// drho_ext/dU
		_JcbDiff[0] = cn*(_phi[_nVar-1] -v2 -p0)/cd;
		for(k=1; k<_nVar-1;k++)
			_JcbDiff[k]=cn*_phi[k]/cd;
		_JcbDiff[_nVar-1]= -cn*_phi[0]/cd;
		//dq_ext/dU
		for(idim=0; idim<_Ndim;idim++)
		{
			//premiere colonne
			_JcbDiff[(1+idim)*_nVar]=-(v2*cn*_phi[idim+1])/(2*cd);
			//colonnes intermediaire
			for(jdim=0; jdim<_Ndim;jdim++)
			{
				_JcbDiff[(1+idim)*_nVar + jdim + 1] =_externalStates[idim+1]*_phi[jdim+1];
				_JcbDiff[(1+idim)*_nVar + jdim + 1]*=cn/cd;
			}
			//matrice identite*cn*(rhoe- p0)
			_JcbDiff[(1+idim)*_nVar + idim + 1] +=( cn*(_phi[0]*_fluides[0]->getInternalEnergy(_externalStates[_nVar-1],rho)-p0))/cd;

			//derniere colonne
			_JcbDiff[(1+idim)*_nVar + _nVar-1]=-_phi[idim+1]*cn/cd;
		}
		//drhoE/dU
		_JcbDiff[_nVar*(_nVar-1)] = -(v2*_phi[_nVar -1]*cn)/(2*cd);
		for(int idim=0; idim<_Ndim;idim++)
			_JcbDiff[_nVar*(_nVar-1)+idim+1]=_externalStates[idim+1]*_phi[_nVar -1]*cn/cd;
		_JcbDiff[_nVar*_nVar -1] = -(v2/2+p0)*cn/cd;
		 */
	}
	else  if (_limitField[nameOfGroup].bcType!=Neumann)// not wall, not inlet, not outlet
	{
		cout << "group named "<<nameOfGroup << " : unknown boundary condition" << endl;
		throw CdmathException("SinglePhase::jacobian: This boundary condition is not treated");
	}
}

//calcule la jacobienne pour une CL de diffusion
void  SinglePhase::jacobianDiff(const int &j, string nameOfGroup)
{
	int k;

	for(k=0; k<_nVar*_nVar;k++)
		_JcbDiff[k] = 0;

	if (_limitField[nameOfGroup].bcType==Wall){
		double v[_Ndim], ve[_Ndim], v2, ve2;

		_idm[0] = _nVar*j;
		for(k=1; k<_nVar; k++)
			_idm[k] = _idm[k-1] + 1;
		VecGetValues(_primitives, _nVar, _idm, _Vj);
		VecGetValues(_courant, _nVar, _idm, _Uj);

		ve[0] = _limitField[nameOfGroup].v_x[0];
		v[0]=_Vj[1];
		ve2 = ve[0]*ve[0];
		v2 = v[0]*v[0];
		if (_Ndim >1){
			ve[1] = _limitField[nameOfGroup].v_y[0];
			v[1]=_Vj[2];
			ve2 += ve[1]*ve[1];
			v2 = v[1]*v[1];
		}
		if (_Ndim >2){
			ve[2] = _limitField[nameOfGroup].v_z[0];
			v[2]=_Vj[3];
			ve2 += ve[2]*ve[2];
			v2 = v[2]*v[2];
		}
		double rho=_Uj[0];
		double internal_energy=_fluides[0]->getInternalEnergy(_limitField[nameOfGroup].T,rho);
		double total_energy=internal_energy+ve2/2;

		//Mass line
		_JcbDiff[0]=v2/(2*internal_energy);
		for(k=0; k<_Ndim;k++)
			_JcbDiff[1+k]=-v[k]/internal_energy;
		_JcbDiff[_nVar-1]=1/internal_energy;
		//Momentum lines
		for(int l =1;l<1+_Ndim;l++){
			_JcbDiff[l*_nVar]=v2*ve[l-1]/(2*internal_energy);
			for(k=0; k<_Ndim;k++)
				_JcbDiff[l*_nVar+1+k]=-v[k]*ve[l-1]/internal_energy;
			_JcbDiff[l*_nVar+_nVar-1]=ve[l-1]/internal_energy;
		}
		//Energy line
		_JcbDiff[(_nVar-1)*_nVar]=v2*total_energy/(2*internal_energy);
		for(k=0; k<_Ndim;k++)
			_JcbDiff[(_nVar-1)*_nVar+1+k]=-v[k]*total_energy/internal_energy;
		_JcbDiff[(_nVar-1)*_nVar+_nVar-1]=total_energy/internal_energy;
	}
	else if (_limitField[nameOfGroup].bcType==Outlet || _limitField[nameOfGroup].bcType==Neumann
			||_limitField[nameOfGroup].bcType==Inlet || _limitField[nameOfGroup].bcType==InletPressure)
	{
		for(k=0;k<_nVar;k++)
			_JcbDiff[k*_nVar+k]=1;
	}
	else{
		cout << "group named "<<nameOfGroup << " : unknown boundary condition" << endl;
		throw CdmathException("SinglePhase::jacobianDiff: This boundary condition is not recognised");
	}
}

void SinglePhase::Prim2Cons(const double *P, const int &i, double *W, const int &j){

	double rho=_fluides[0]->getDensity(P[i*(_Ndim+2)], P[i*(_Ndim+2)+(_Ndim+1)]);
	W[j*(_Ndim+2)] =  _porosityField(j)*rho;//phi*rho
	for(int k=0; k<_Ndim; k++)
		W[j*(_Ndim+2)+(k+1)] = W[j*(_Ndim+2)]*P[i*(_Ndim+2)+(k+1)];//phi*rho*u

	W[j*(_Ndim+2)+(_Ndim+1)] = W[j*(_Ndim+2)]*_fluides[0]->getInternalEnergy(P[i*(_Ndim+2)+ (_Ndim+1)],rho);//rho*e
	for(int k=0; k<_Ndim; k++)
		W[j*(_Ndim+2)+(_Ndim+1)] += W[j*(_Ndim+2)]*P[i*(_Ndim+2)+(k+1)]*P[i*(_Ndim+2)+(k+1)]*0.5;//phi*rho*e+0.5*phi*rho*u^2
}

void SinglePhase::consToPrim(const double *Wcons, double* Wprim,double porosity)
{
	double q_2 = 0;
	for(int k=1;k<=_Ndim;k++)
		q_2 += Wcons[k]*Wcons[k];
	q_2 /= Wcons[0];	//phi rho u²
	double rhoe=(Wcons[(_Ndim+2)-1]-q_2/2)/porosity;
	double rho=Wcons[0]/porosity;
	Wprim[0] =  _fluides[0]->getPressure(rhoe,rho);//pressure p
	if (Wprim[0]<0){
		cout << "pressure = "<< Wprim[0] << " < 0 " << endl;
		throw CdmathException("SinglePhase::consToPrim: negative pressure");
	}
	for(int k=1;k<=_Ndim;k++)
		Wprim[k] = Wcons[k]/Wcons[0];//velocity u
	Wprim[(_Ndim+2)-1] =  _fluides[0]->getTemperatureFromPressure(Wprim[0],Wcons[0]/porosity);
}

void SinglePhase::entropicShift(double* n)//TO do: make sure _Vi and _Vj are well set
{
	/*Left and right values */
	double ul_n = 0, ul_2=0, ur_n=0,	ur_2 = 0; //valeurs de vitesse normale et |u|² a droite et a gauche
	for(int i=0;i<_Ndim;i++)
	{
		ul_n += _Vi[1+i]*n[i];
		ul_2 += _Vi[1+i]*_Vi[1+i];
		ur_n += _Vj[1+i]*n[i];
		ur_2 += _Vj[1+i]*_Vj[1+i];
	}


	double cl = _fluides[0]->vitesseSonEnthalpie(_Vi[_Ndim+1]-ul_2/2);//vitesse du son a l'interface
	double cr = _fluides[0]->vitesseSonEnthalpie(_Vj[_Ndim+1]-ur_2/2);//vitesse du son a l'interface

	_entropicShift[0]=abs(ul_n-cl - (ur_n-cr));
	_entropicShift[1]=abs(ul_n     - ur_n);
	_entropicShift[2]=abs(ul_n+cl - (ur_n+cr));

	if(_verbose && _nbTimeStep%_freqSave ==0)
	{
		cout<<"Entropic shift left eigenvalues: "<<endl;
		cout<<"("<< ul_n-cl<< ", " <<ul_n<<", "<<ul_n+cl << ")";
		cout<<endl;
		cout<<"Entropic shift right eigenvalues: "<<endl;
		cout<<"("<< ur_n-cr<< ", " <<ur_n<<", "<<ur_n+cr << ")";
		cout<< endl;
		cout<<"eigenvalue jumps "<<endl;
		cout<< _entropicShift[0] << ", " << _entropicShift[1] << ", "<< _entropicShift[2] <<endl;
	}
}

Vector SinglePhase::convectionFlux(Vector U,Vector V, Vector normale, double porosity){
	if(_verbose){
		cout<<"Ucons"<<endl;
		cout<<U<<endl;
		cout<<"Vprim"<<endl;
		cout<<V<<endl;
	}

	double phirho=U(0);//phi rho
	Vector phiq(_Ndim);//phi rho u
	for(int i=0;i<_Ndim;i++)
		phiq(i)=U(1+i);

	double pression=V(0);
	Vector vitesse(_Ndim);
	for(int i=0;i<_Ndim;i++)
		vitesse(i)=V(1+i);
	double Temperature= V(1+_Ndim);

	double vitessen=vitesse*normale;
	double rho=phirho/porosity;
	double e_int=_fluides[0]->getInternalEnergy(Temperature,rho);

	Vector F(_nVar);
	F(0)=phirho*vitessen;
	for(int i=0;i<_Ndim;i++)
		F(1+i)=phirho*vitessen*vitesse(i)+pression*normale(i)*porosity;
	F(1+_Ndim)=phirho*(e_int+0.5*vitesse*vitesse+pression/rho)*vitessen;

	if(_verbose){
		cout<<"Flux F(U,V)"<<endl;
		cout<<F<<endl;
	}

	return F;
}

Vector SinglePhase::staggeredVFFCFlux()
{
	if(_spaceScheme!=staggered || _nonLinearFormulation!=VFFC)
		throw CdmathException("SinglePhase::staggeredVFFCFlux: staggeredVFFCFlux method should be called only for VFFC formulation and staggered upwinding");
	else//_spaceScheme==staggered && _nonLinearFormulation==VFFC
	{
		Vector Fij(_nVar);

		double uijn=0, phiqn=0;
		for(int idim=0;idim<_Ndim;idim++)
			uijn+=_vec_normal[idim]*_Uroe[1+idim];//URoe = rho, u, H

		if(uijn>=0)
		{
			for(int idim=0;idim<_Ndim;idim++)
				phiqn+=_vec_normal[idim]*_Ui[1+idim];//phi rho u n
			Fij(0)=phiqn;
			for(int idim=0;idim<_Ndim;idim++)
				Fij(1+idim)=phiqn*_Vi[1+idim]+_Vj[0]*_vec_normal[idim]*_porosityj;
			Fij(_nVar-1)=phiqn/_Ui[0]*(_Ui[_nVar-1]+_Vj[0]*sqrt(_porosityj/_porosityi));
		}
		else
		{
			for(int idim=0;idim<_Ndim;idim++)
				phiqn+=_vec_normal[idim]*_Uj[1+idim];//phi rho u n
			Fij(0)=phiqn;
			for(int idim=0;idim<_Ndim;idim++)
				Fij(1+idim)=phiqn*_Vj[1+idim]+_Vi[0]*_vec_normal[idim]*_porosityi;
			Fij(_nVar-1)=phiqn/_Uj[0]*(_Uj[_nVar-1]+_Vi[0]*sqrt(_porosityi/_porosityj));
		}
		return Fij;
	}
}

void SinglePhase::staggeredVFFCMatrices(double un)//vitesse normale de Roe en entrée
{
	if(_spaceScheme!=staggered || _nonLinearFormulation!=VFFC)
		throw CdmathException("SinglePhase::staggeredVFFCFlux: staggeredVFFCFlux method should be called only for VFFC formulation and staggered upwinding");
	else//_spaceScheme==staggered && _nonLinearFormulation==VFFC
	{
		double ui_n=0, ui_2=0, uj_n=0, uj_2=0;//vitesse normale et carré du module
		double H;//enthalpie totale (expression particulière)
		for(int i=0;i<_Ndim;i++)
		{
			ui_2 += _Vi[1+i]*_Vi[1+i];
			ui_n += _Vi[1+i]*_vec_normal[i];
			uj_2 += _Vj[1+i]*_Vj[1+i];
			uj_n += _Vj[1+i]*_vec_normal[i];
		}

		if(un>=0)
		{
			double rhoi,pj, Ei, rhoj;
			double cj, Kj, kj;//dérivées de la pression
			rhoi=_Ui[0]/_porosityi;
			Ei= _Ui[_Ndim+1]/(rhoi*_porosityi);
			pj=_Vj[0];
			rhoj=_Uj[0]/_porosityj;
			H =Ei+pj/rhoi;
			cj = _fluides[0]->vitesseSonTemperature(_Vj[_Ndim+1],rhoj);
			kj = _fluides[0]->constante("gamma") - 1;//A generaliser pour porosite et stephane gas law
			Kj = uj_2*kj/2; //g-1/2 *|u|²

			/***********Calcul des valeurs propres ********/
			vector<complex<double> > vp_dist(3,0);
			vp_dist[0]=ui_n-cj;vp_dist[1]=ui_n;vp_dist[2]=ui_n+cj;

			_maxvploc=fabs(ui_n)+cj;
			if(_maxvploc>_maxvp)
				_maxvp=_maxvploc;

			if(_verbose && _nbTimeStep%_freqSave ==0)
				cout<<"Valeurs propres "<<ui_n-cj<<" , "<<ui_n<<" , "<<ui_n+cj<<endl;

			/******** Construction de la matrice A^+ *********/
			//premiere ligne (masse)
			_AroePlus[0]=0;
			for(int idim=0; idim<_Ndim;idim++)
				_AroePlus[1+idim]=_vec_normal[idim];
			_AroePlus[_nVar-1]=0;

			//lignes intermadiaires(qdm)
			for(int idim=0; idim<_Ndim;idim++)
			{
				//premiere colonne
				_AroePlus[(1+idim)*_nVar]=- ui_n*_Ui[1+idim];
				//colonnes intermediaires
				for(int jdim=0; jdim<_Ndim;jdim++)
					_AroePlus[(1+idim)*_nVar + jdim + 1] = _Ui[1+idim]*_vec_normal[jdim];
				//matrice identite
				_AroePlus[(1+idim)*_nVar + idim + 1] += ui_n;
				//derniere colonne
				_AroePlus[(1+idim)*_nVar + _nVar-1]=0;
			}

			//derniere ligne (energie)
			_AroePlus[_nVar*(_nVar-1)] = - H*ui_n;
			for(int idim=0; idim<_Ndim;idim++)
				_AroePlus[_nVar*(_nVar-1)+idim+1]=H*_vec_normal[idim] ;
			_AroePlus[_nVar*_nVar -1] = ui_n;

			/******** Construction de la matrice A^- *********/
			//premiere ligne (masse)
			_AroeMinus[0]=0;
			for(int idim=0; idim<_Ndim;idim++)
				_AroeMinus[1+idim]=0;
			_AroeMinus[_nVar-1]=0;

			//lignes intermadiaires(qdm)
			for(int idim=0; idim<_Ndim;idim++)
			{
				//premiere colonne
				_AroeMinus[(1+idim)*_nVar]=Kj*_vec_normal[idim] ;
				//colonnes intermediaires
				for(int jdim=0; jdim<_Ndim;jdim++)
					_AroeMinus[(1+idim)*_nVar + jdim + 1] = -kj*_vec_normal[idim]*_Uj[1+jdim];
				//matrice identite
				_AroeMinus[(1+idim)*_nVar + idim + 1] += 0;
				//derniere colonne
				_AroeMinus[(1+idim)*_nVar + _nVar-1]=kj*_vec_normal[idim];
			}

			//derniere ligne (energie)
			_AroeMinus[_nVar*(_nVar-1)] = Kj *ui_n;
			for(int idim=0; idim<_Ndim;idim++)
				_AroeMinus[_nVar*(_nVar-1)+idim+1]= - kj*uj_n*_Ui[idim+1];
			_AroeMinus[_nVar*_nVar -1] = kj*ui_n;
		}
		else
		{
			double rhoi,pi, Ej, rhoj;
			double ci, Ki, ki;//dérivées de la pression
			rhoj=_Uj[0]/_porosityj;
			Ej= _Uj[_Ndim+1]/rhoj;
			pi=_Vi[0];
			rhoi=_Ui[0]/_porosityi;
			H =Ej+pi/rhoj;
			ci = _fluides[0]->vitesseSonTemperature(_Vi[_Ndim+1],rhoi);
			ki = _fluides[0]->constante("gamma") - 1;//A generaliser pour porosite et stephane gas law
			Ki = ui_2*ki/2; //g-1/2 *|u|²

			/***********Calcul des valeurs propres ********/
			vector<complex<double> > vp_dist(3,0);
			vp_dist[0]=uj_n-ci;vp_dist[1]=uj_n;vp_dist[2]=uj_n+ci;

			_maxvploc=fabs(uj_n)+ci;
			if(_maxvploc>_maxvp)
				_maxvp=_maxvploc;

			if(_verbose && _nbTimeStep%_freqSave ==0)
				cout<<"Valeurs propres "<<uj_n-ci<<" , "<<uj_n<<" , "<<uj_n+ci<<endl;

			/******** Construction de la matrice A^+ *********/
			//premiere ligne (masse)
			_AroePlus[0]=0;
			for(int idim=0; idim<_Ndim;idim++)
				_AroePlus[1+idim]=0;
			_AroePlus[_nVar-1]=0;

			//lignes intermadiaires(qdm)
			for(int idim=0; idim<_Ndim;idim++)
			{
				//premiere colonne
				_AroePlus[(1+idim)*_nVar]=Ki*_vec_normal[idim] ;
				//colonnes intermediaires
				for(int jdim=0; jdim<_Ndim;jdim++)
					_AroePlus[(1+idim)*_nVar + jdim + 1] = -ki*_vec_normal[idim]*_Ui[1+jdim];
				//matrice identite
				_AroePlus[(1+idim)*_nVar + idim + 1] += 0;
				//derniere colonne
				_AroePlus[(1+idim)*_nVar + _nVar-1]=ki*_vec_normal[idim];
			}

			//derniere ligne (energie)
			_AroePlus[_nVar*(_nVar-1)] = Ki *uj_n;
			for(int idim=0; idim<_Ndim;idim++)
				_AroePlus[_nVar*(_nVar-1)+idim+1]= - ki*ui_n*_Uj[idim+1];
			_AroePlus[_nVar*_nVar -1] =  ki*uj_n;

			/******** Construction de la matrice A^- *********/
			//premiere ligne (masse)
			_AroeMinus[0]=0;
			for(int idim=0; idim<_Ndim;idim++)
				_AroeMinus[1+idim]=_vec_normal[idim];
			_AroeMinus[_nVar-1]=0;

			//lignes intermadiaires(qdm)
			for(int idim=0; idim<_Ndim;idim++)
			{
				//premiere colonne
				_AroeMinus[(1+idim)*_nVar]= - uj_n*_Uj[1+idim];
				//colonnes intermediaires
				for(int jdim=0; jdim<_Ndim;jdim++)
					_AroeMinus[(1+idim)*_nVar + jdim + 1] = _Uj[1+idim]*_vec_normal[jdim];
				//matrice identite
				_AroeMinus[(1+idim)*_nVar + idim + 1] += uj_n;
				//derniere colonne
				_AroeMinus[(1+idim)*_nVar + _nVar-1]=0;
			}

			//derniere ligne (energie)
			_AroeMinus[_nVar*(_nVar-1)] = - H*uj_n;
			for(int idim=0; idim<_Ndim;idim++)
				_AroeMinus[_nVar*(_nVar-1)+idim+1]=H*_vec_normal[idim] ;
			_AroeMinus[_nVar*_nVar -1] = uj_n;
		}
	}
	/******** Construction de la matrices Aroe *********/
	/*
	//premiere ligne (masse)
	_Aroe[0]=0;
	for(int idim=0; idim<_Ndim;idim++)
		_Aroe[1+idim]=_vec_normal[idim];
	_Aroe[_nVar-1]=0;

	//lignes intermadiaires(qdm)
	for(int idim=0; idim<_Ndim;idim++)
	{
		//premiere colonne
		_Aroe[(1+idim)*_nVar]=Ki*_vec_normal[idim] - uj_n*_Uj[1+idim];
		//colonnes intermediaires
		for(int jdim=0; jdim<_Ndim;jdim++)
			_Aroe[(1+idim)*_nVar + jdim + 1] = _Uj[1+idim]*_vec_normal[jdim]-ki*_vec_normal[idim]*_Ui[1+jdim];
		//matrice identite
		_Aroe[(1+idim)*_nVar + idim + 1] += uj_n;
		//derniere colonne
		_Aroe[(1+idim)*_nVar + _nVar-1]=ki*_vec_normal[idim];
	}

	//derniere ligne (energie)
	_Aroe[_nVar*(_nVar-1)] = (Ki - H)*uj_n;
	for(int idim=0; idim<_Ndim;idim++)
		_Aroe[_nVar*(_nVar-1)+idim+1]=H*_vec_normal[idim] - ki*ui_n*_Uj[idim+1];
	_Aroe[_nVar*_nVar -1] = (1 + ki)*uj_n;
	 */

}
void SinglePhase::applyVFRoeLowMachCorrections()
{
	if(_nonLinearFormulation!=VFRoe)
		throw CdmathException("SinglePhase::applyVFRoeLowMachCorrections: applyVFRoeLowMachCorrections method should be called only for VFRoe formulation");
	else//_nonLinearFormulation==VFRoe
	{
		if(_spaceScheme==lowMach){
			double u_2=0;
			for(int i=0;i<_Ndim;i++)
				u_2 += _Uroe[1+i]*_Uroe[1+i];
			double 	c = _maxvploc;//vitesse du son a l'interface
			double M=sqrt(u_2)/c;//Mach number
			_Vij[0]=M*_Vij[0]+(1-M)*(_Vi[0]+_Vj[0])/2;
			Prim2Cons(_Vij,0,_Uij,0);
		}
		else if(_spaceScheme==pressureCorrection)
		{
			double norm_uij=0, uij_n=0, ui_n=0, uj_n=0;
			for(int i=0;i<_Ndim;i++)
			{
				norm_uij += _Uroe[1+i]*_Uroe[1+i];
				uij_n += _Uroe[1+i]*_vec_normal[i];
				ui_n += _Vi[1+i]*_vec_normal[i];
				uj_n += _Vj[1+i]*_vec_normal[i];
			}
			norm_uij=sqrt(norm_uij);
			_Vij[0]=(_Vi[0]+_Vj[0])/2 + uij_n/norm_uij*(_Vj[0]-_Vi[0])/4 - _Uroe[0]*norm_uij*(uj_n-ui_n)/4;
		}
		else if(_spaceScheme==staggered)
		{
			double uij_n=0;
			for(int i=0;i<_Ndim;i++)
				uij_n += _Uroe[1+i]*_vec_normal[i];

			if(uij_n>=0){
				_Vij[0]=_Vj[0];
				for(int i=0;i<_Ndim;i++)
					_Vij[1+i]=_Vi[1+i];
				_Vij[_nVar-1]=_Vi[_nVar-1];
			}
			else{
				_Vij[0]=_Vi[0];
				for(int i=0;i<_Ndim;i++)
					_Vij[1+i]=_Vj[1+i];
				_Vij[_nVar-1]=_Vj[_nVar-1];
			}
			Prim2Cons(_Vij,0,_Uij,0);
		}
	}
}

void SinglePhase::testConservation()
{
	double SUM, DELTA, x;
	int I;
	for(int i=0; i<_nVar; i++)
	{
		{
			if(i == 0)
				cout << "Masse totale (kg): ";
			else
			{
				if(i == _nVar-1)
					cout << "Energie totale (J): ";
				else
					cout << "Quantite de mouvement totale (kg.m.s^-1): ";
			}
		}
		SUM = 0;
		I =  i;
		DELTA = 0;
		for(int j=1; j<=_Nmailles; j++)
		{
			VecGetValues(_courant, 1, &I, &x);//on recupere la valeur du champ
			SUM += x*_mesh.getCell(j-1).getMeasure();
			VecGetValues(_next, 1, &I, &x);//on recupere la variation du champ
			DELTA += x*_mesh.getCell(j-1).getMeasure();
			I += _nVar;
		}
		{
			if(fabs(SUM)>_precision)
				cout << SUM << ", variation relative: " << fabs(DELTA /SUM)  << endl;
			else
				cout << " a une somme quasi nulle,  variation absolue: " << fabs(DELTA) << endl;
		}
	}
}

void SinglePhase::save(){
	string prim(_path+"/SinglePhasePrim_");///Results
	string cons(_path+"/SinglePhaseCons_");
	prim+=_fileName;
	cons+=_fileName;

	PetscInt Ii;
	for (long i = 0; i < _Nmailles; i++){
		// j = 0 : pressure; j = _nVar - 1: temperature; j = 1,..,_nVar-2: velocity
		for (int j = 0; j < _nVar; j++){
			Ii = i*_nVar +j;
			VecGetValues(_primitives,1,&Ii,&_VV(i,j));
		}
	}
	if(_saveConservativeField){
		for (long i = 0; i < _Nmailles; i++){
			// j = 0 : density; j = _nVar - 1 : energy j = 1,..,_nVar-2: momentum
			for (int j = 0; j < _nVar; j++){
				Ii = i*_nVar +j;
				VecGetValues(_courant,1,&Ii,&_UU(i,j));
			}
		}
		_UU.setTime(_time,_nbTimeStep);
	}
	_VV.setTime(_time,_nbTimeStep);

	// create mesh and component info
	if (_nbTimeStep ==0){
		string prim_suppress ="rm -rf "+prim+"_*";
		string cons_suppress ="rm -rf "+cons+"_*";
		system(prim_suppress.c_str());//Nettoyage des précédents calculs identiques
		system(cons_suppress.c_str());//Nettoyage des précédents calculs identiques
		if(_saveConservativeField){
			_UU.setInfoOnComponent(0,"Density (kg/m^3)");
			_UU.setInfoOnComponent(1,"Momentum_x");// (kg/m^2/s)
			if (_Ndim>1)
				_UU.setInfoOnComponent(2,"Momentum_y");// (kg/m^2/s)
			if (_Ndim>2)
				_UU.setInfoOnComponent(3,"Momentum_z");// (kg/m^2/s)

			_UU.setInfoOnComponent(_nVar-1,"Energy (J/m^3)");

			switch(_saveFormat)
			{
			case VTK :
				_UU.writeVTK(cons);
				break;
			case MED :
				_UU.writeMED(cons);
				break;
			case CSV :
				_UU.writeCSV(cons);
				break;
			}
		}
		_VV.setInfoOnComponent(0,"Pressure (Pa)");
		_VV.setInfoOnComponent(1,"Velocity_x (m/s)");
		if (_Ndim>1)
			_VV.setInfoOnComponent(2,"Velocity_y (m/s)");
		if (_Ndim>2)
			_VV.setInfoOnComponent(3,"Velocity_z (m/s)");
		_VV.setInfoOnComponent(_nVar-1,"Temperature (K)");

		switch(_saveFormat)
		{
		case VTK :
			_VV.writeVTK(prim);
			break;
		case MED :
			_VV.writeMED(prim);
			break;
		case CSV :
			_VV.writeCSV(prim);
			break;
		}
	}
	// do not create mesh
	else{
		switch(_saveFormat)
		{
		case VTK :
			_VV.writeVTK(prim,false);
			break;
		case MED :
			_VV.writeMED(prim,false);
			break;
		case CSV :
			_VV.writeCSV(prim);
			break;
		}
		if(_saveConservativeField){
			switch(_saveFormat)
			{
			case VTK :
				_UU.writeVTK(cons,false);
				break;
			case MED :
				_UU.writeMED(cons,false);
				break;
			case CSV :
				_UU.writeCSV(cons);
				break;
			}
		}
	}
	if(_saveVelocity){
		for (long i = 0; i < _Nmailles; i++){
			// j = 0 : pressure; j = _nVar - 1: temperature; j = 1,..,_nVar-2: velocity
			for (int j = 0; j < _Ndim; j++)//On récupère les composantes de vitesse
			{
				int Ii = i*_nVar +1+j;
				VecGetValues(_primitives,1,&Ii,&_Vitesse(i,j));
			}
			for (int j = _Ndim; j < 3; j++)//On met à zero les composantes de vitesse si la dimension est <3
				_Vitesse(i,j)=0;
		}
		_Vitesse.setTime(_time,_nbTimeStep);
		if (_nbTimeStep ==0){
			_Vitesse.setInfoOnComponent(0,"Velocity_x (m/s)");
			_Vitesse.setInfoOnComponent(1,"Velocity_y (m/s)");
			_Vitesse.setInfoOnComponent(2,"Velocity_z (m/s)");

			switch(_saveFormat)
			{
			case VTK :
				_Vitesse.writeVTK(prim+"_Velocity");
				break;
			case MED :
				_Vitesse.writeMED(prim+"_Velocity");
				break;
			case CSV :
				_Vitesse.writeCSV(prim+"_Velocity");
				break;
			}
		}
		else{
			switch(_saveFormat)
			{
			case VTK :
				_Vitesse.writeVTK(prim+"_Velocity",false);
				break;
			case MED :
				_Vitesse.writeMED(prim+"_Velocity",false);
				break;
			case CSV :
				_Vitesse.writeCSV(prim+"_Velocity");
				break;
			}
		}
	}
}