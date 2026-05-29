const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const bundlePath = path.join(root, 'viewer', 'bundle.js');
const scenePath = path.join(root, 'viewer', '3.bundle.js');

let bundle = fs.readFileSync(bundlePath, 'utf8');
const boatModelOld = 'this.leftfoil="",this.rightfoil=""}return e.prototype.deserialize';
const boatModelNew = 'this.leftfoil="",this.rightfoil="",this.hullType=""}return e.prototype.deserialize';
if (!bundle.includes(boatModelOld)) throw new Error('bundle.js boatModel pattern not found');
bundle = bundle.replace(boatModelOld, boatModelNew);
fs.writeFileSync(bundlePath, bundle);

let scene = fs.readFileSync(scenePath, 'utf8');

const ctorOld = 'function e(e,t,r,i,l,c,h,d,u,p){';
const ctorNew = 'function e(e,t,r,i,l,c,h,d,u,p,v){';
if (!scene.includes(ctorOld)) throw new Error('constructor signature not found');
scene = scene.replace(ctorOld, ctorNew);

const foilAssignOld =
  'this.leftFoil=new L(this.raceScene,this.boatMesh,this.leftFoilName,!0),this.rightFoil=new L(this.raceScene,this.boatMesh,this.rightFoilName,!1)},e.prototype.getAllMorphs';
const foilAssignNew =
  '""!==this.leftFoilName&&(this.leftFoil=new L(this.raceScene,this.boatMesh,this.leftFoilName,!0)),""!==this.rightFoilName&&(this.rightFoil=new L(this.raceScene,this.boatMesh,this.rightFoilName,!1))},e.prototype.getAllMorphs';
if (!scene.includes(foilAssignOld)) throw new Error('foil assign pattern not found');
scene = scene.replace(foilAssignOld, foilAssignNew);

const wakesOld =
  'this.boatWakes=new Array(3),this.boatWakes[0]=new s(e,this,a.Central,new n.Vector3(0,0,.1),30),this.boatWakes[1]=new s(e,this,a.LeftFoil,new n.Vector3(-1.1,5,.07),12),this.boatWakes[2]=new s(e,this,a.RightFoil,new n.Vector3(-1.1,-5,.06),12),this.boatWakes[3]=new s(e,this,a.Undefined,new n.Vector3(9,0,.05),12),';
const wakesNew =
  'this.hullType=(v||"").toLowerCase(),this.boatWakes=function(e,t,o,l,c,h){var d=l.length>0,f=c.length>0;h||(h=d&&f?"multihull":"monohull");var u=[new s(e,t,o.Central,new n.Vector3(0,0,.1),30)];if("monohull"===h)return u;if(d&&u.push(new s(e,t,o.LeftFoil,new n.Vector3(-1.1,5,.07),12)),f&&u.push(new s(e,t,o.RightFoil,new n.Vector3(-1.1,-5,.06),12)),d&&f)u.push(new s(e,t,o.Undefined,new n.Vector3(9,0,.05),12));else for(var p=0,w=[[-1.1,5,.07],[-1.1,-5,.06]];p<w.length;p++)u.push(new s(e,t,o.Undefined,new n.Vector3(w[p][0],w[p][1],w[p][2]),12));return u}(e,this,a,u,p,this.hullType),';
if (!scene.includes(wakesOld)) throw new Error('boatWakes pattern not found');
scene = scene.replace(wakesOld, wakesNew);

const newAOld = 'c.jibtarget,c.mainsailtarget,c.leftfoil,c.rightfoil)).setSpeedArrowEnabled';
const newANew = 'c.jibtarget,c.mainsailtarget,c.leftfoil,c.rightfoil,c.hullType||"")).setSpeedArrowEnabled';
if (!scene.includes(newAOld)) throw new Error('new A call not found');
scene = scene.replace(newAOld, newANew);

fs.writeFileSync(scenePath, scene);
console.log('Patches applied OK');
