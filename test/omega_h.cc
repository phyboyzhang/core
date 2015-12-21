#include <apf.h>
#include <gmi_mesh.h>
#include <apfMDS.h>
#include <apfMesh2.h>
#include <PCU.h>
#include <apfOmega_h.h>
#include <cstdlib>

int main(int argc, char** argv)
{
  MPI_Init(&argc,&argv);
  PCU_Comm_Init();
  if ( argc != 4 ) {
    if ( !PCU_Comm_Self() )
      printf("Usage: %s <model> <in.smb> <out.vtu>\n", argv[0]);
    MPI_Finalize();
    exit(EXIT_FAILURE);
  }
  gmi_register_mesh();
  apf::Mesh2* m = apf::loadMdsMesh(argv[1],argv[2]);
  osh_t om = osh::fromAPF(m);
  m->destroyNative();
  apf::destroyMesh(m);
  osh_write_vtk(om, argv[3]);
  m = apf::makeEmptyMdsMesh(gmi_load(argv[1]), osh_dim(om), false);
  osh::toAPF(om, m);
  m->verify();
  osh_free(om);
  m->destroyNative();
  apf::destroyMesh(m);
  PCU_Comm_Free();
  MPI_Finalize();
}

