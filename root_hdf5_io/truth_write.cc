#include <cstring>
#include <iostream>
#include <iterator>
#include <numeric>
#include <string>
#include <vector>

#include "canvas/Utilities/InputTag.h"
#include "canvas/Persistency/Common/Assns.h"
#include "canvas/Persistency/Provenance/EventAuxiliary.h"
#include "gallery/Event.h"
#include "gallery/ValidHandle.h"
#include "lardataobj/RecoBase/Cluster.h"
#include "lardataobj/RecoBase/Vertex.h"
#include "lardataobj/MCBase/MCHitCollection.h"
#include "nusimdata/SimulationBase/MCTruth.h"

#include "h5fnal.h"

#define MASTER_RUN_CONTAINER    "master_run_container"
#define BADNAME                 "MCTRUTH"     // TODO: Replace this with a good name

using namespace art;
using namespace std;
using namespace simb;

int main(int argc, char* argv[]) {

    size_t totalTruths = 0L;
    hid_t   fid 		= H5FNAL_BAD_HID_T;
    hid_t   fapl_id 	= H5FNAL_BAD_HID_T;
    hid_t   master_id = H5FNAL_BAD_HID_T;
    hid_t   run_id 	= H5FNAL_BAD_HID_T;
    hid_t   subrun_id = H5FNAL_BAD_HID_T;
    hid_t   event_id 	= H5FNAL_BAD_HID_T;
    hid_t   dict_id 	= H5FNAL_BAD_HID_T;
    int prevRun 		= -1;
    int prevSubRun 	= -1;
    string_dictionary_t *dict = NULL;
    h5fnal_vect_truth_t *h5vtruth = NULL;
 
    InputTag mchits_tag { "mchitfinder" };
    InputTag vertex_tag { "linecluster" };
    InputTag assns_tag  { "linecluster" };
    InputTag truths_tag { "generator" };

    vector<string> filenames { argv+1, argv+argc }; // filenames from command line
    if (2 != filenames.size()) {
        std::cerr << "Please supply input and output filenames\n";
        exit(EXIT_FAILURE);
    }

    /* Create the HDF5 file */
    string h5FileName = filenames.back();
    filenames.pop_back();
    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        H5FNAL_HDF5_ERROR;
    if (H5Pset_libver_bounds(fapl_id, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST) < 0)
        H5FNAL_HDF5_ERROR;
    if ((fid = H5Fcreate(h5FileName.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id)) < 0)
        H5FNAL_HDF5_ERROR;

    /* Create a file-wide string dictionary */
    if (NULL == (dict = (string_dictionary_t *)calloc(1, sizeof(string_dictionary_t))))
        H5FNAL_PROGRAM_ERROR("could not get memory for string dictionary");
    if ((dict_id = create_string_dictionary(fid, dict)) < 0)
        H5FNAL_PROGRAM_ERROR("could not create string dictionary");

    /* Create a top-level containing group in which creation order is tracked and indexed.
     * There is no way to do this in the root group, so we can't use that.
     */
    if ((master_id = h5fnal_create_run(fid, MASTER_RUN_CONTAINER, FALSE)) < 0)
        H5FNAL_PROGRAM_ERROR("could not create master run containing group");

    /* Allocate memory for the data product struct
     *
     * This will be re-used for all data products we read in root and create
     * in HDF5 (it's re-initialized on close).
     */
    if (NULL == (h5vtruth = (h5fnal_vect_truth_t *)calloc(1, sizeof(h5fnal_vect_truth_t))))
        H5FNAL_PROGRAM_ERROR("could not get memory for HDF5 data product struct");

    // The gallery::Event object acts as a cursor into the stream of events.
    // A newly-constructed gallery::Event is at the start if its stream.
    // Use gallery::Event::atEnd() to check if you've reached the end of the stream.
    // Use gallery::Event::next() to go to the next event.

    // Loop over all the events in the root file
    for (gallery::Event ev(filenames); !ev.atEnd(); ev.next()) {
        auto const& aux = ev.eventAuxiliary();

        vector<h5fnal_truth_t> truths;
        vector<h5fnal_trajectory_t> trajectories;
        vector<h5fnal_daughter_t> daughters;
        vector<h5fnal_particle_t> particles;
        vector<h5fnal_neutrino_t> neutrinos;

        h5fnal_vect_truth_data_t truth_data;

        std::cout << "Processing event: " << aux.run()
                  << ',' << aux.subRun()
                  << ',' << aux.event() << '\n';
  
        unsigned int currentRun = aux.run();
        unsigned int currentSubRun = aux.subRun();

        if ((int)currentRun != prevRun) {
            // Create a new run (create name from the integer ID)
            if (run_id != H5FNAL_BAD_HID_T)
                if (h5fnal_close_run(run_id) < 0)
                    H5FNAL_PROGRAM_ERROR("could not close run")

            if ((run_id = h5fnal_create_run(master_id, std::to_string(currentRun).c_str(), FALSE)) < 0)
                H5FNAL_PROGRAM_ERROR("could not create run");

            // Create a new sub-run (create name from the integer ID)
            if (subrun_id != H5FNAL_BAD_HID_T)
                if (h5fnal_close_run(subrun_id) < 0)
                    H5FNAL_PROGRAM_ERROR("could not close sub-run");

            if ((subrun_id = h5fnal_create_run(run_id, std::to_string(currentSubRun).c_str(), FALSE)) < 0)
                H5FNAL_PROGRAM_ERROR("could not create sub-run");

            prevRun = currentRun;
            prevSubRun = currentSubRun;
        }
        else if ((int)currentSubRun != prevSubRun) {
            // make new group for SubRun in the same run
            if (subrun_id != H5FNAL_BAD_HID_T)
                if (h5fnal_close_run(subrun_id) < 0)
                    H5FNAL_PROGRAM_ERROR("could not close sub-run");

            if ((subrun_id = h5fnal_create_run(run_id, std::to_string(currentSubRun).c_str(), FALSE)) < 0)
                H5FNAL_PROGRAM_ERROR("could not create sub-run");

            prevSubRun = currentSubRun;
        }

        // Create a new event (create name from the integer ID)
        unsigned int currentEvent = aux.event();
        if ((event_id = h5fnal_create_event(subrun_id, std::to_string(currentEvent).c_str(), FALSE)) < 0)
            H5FNAL_PROGRAM_ERROR("could not create event");
   
        // getValidHandle() is preferred to getByLabel(), for both art and
        // gallery use. It does not require in-your-face error handling.
        std::vector<simb::MCTruth> const& rootTruths = *ev.getValidHandle<vector<simb::MCTruth>>(truths_tag);

        // Create the Vector of MC Truth via h5fnal.
        // This will create a group containing datasets. The name for this group should be something like:
        // "MCHitCollections_mchitfinder_"
        // The empty string following the 2nd underscore indicates and empty 'product instance name'.
        // There is no need to represent the 'process name' because that is a top-level of the file entity -- in the root group.
        // TODO: Update the name (using a cheap, hard-coded name for now)
        if (h5fnal_create_v_mc_truth(event_id, BADNAME, h5vtruth) < 0)
            H5FNAL_PROGRAM_ERROR("could not create HDF5 data product");

        // Iterate through all truths in the vector
        totalTruths += rootTruths.size();
        for (unsigned n = 0; n < rootTruths.size(); n++) {

            simb::MCTruth t = rootTruths[n];
            h5fnal_truth_t truth;

            hsize_t new_particles = 0;

            // Copy origin
            truth.origin = static_cast<h5fnal_origin_t>(t.Origin());

            // Copy particles
            for (int i = 0; i < t.NParticles(); i++) {
                const simb::MCParticle& p = t.GetParticle(i);
                h5fnal_particle_t particle;
                hsize_t new_trajectories = 0;
                hsize_t new_daughters = 0;
                hbool_t string_found;
                unsigned string_index;

                particle.status     = p.StatusCode();
                particle.track_id   = p.TrackId();
                particle.pdg_code   = p.PdgCode();
                particle.mother     = p.Mother();
                particle.mass       = p.Mass();
                particle.weight     = p.Weight();
                particle.gvtx_x     = p.Gvx();
                particle.gvtx_y     = p.Gvy();
                particle.gvtx_z     = p.Gvz();
                particle.gvtx_t     = p.Gvt();
                particle.rescatter  = p.Rescatter();

                // Get polarization TVector3 and set fields
                const TVector3& pol = p.Polarization();
                particle.polarization_x = pol.x();
                particle.polarization_y = pol.y();
                particle.polarization_z = pol.z();

                // Store the process string
                if (get_string_index(p.Process().c_str(), dict, &string_found, &string_index) < 0)
                    H5FNAL_PROGRAM_ERROR("error getting Process string index");
                if (!string_found)
                    if (add_string_to_dictionary(p.Process().c_str(), dict) < 0)
                        H5FNAL_PROGRAM_ERROR("error adding Process string to dictionary");
                particle.process_index = static_cast<hsize_t>(string_index);

                // Store the end process string
                if (get_string_index(p.EndProcess().c_str(), dict, &string_found, &string_index) < 0)
                    H5FNAL_PROGRAM_ERROR("error getting EndProcess string index");
                if (!string_found)
                    if (add_string_to_dictionary(p.EndProcess().c_str(), dict) < 0)
                        H5FNAL_PROGRAM_ERROR("error adding EndProcess string to dictionary");
                particle.endprocess_index = static_cast<hsize_t>(string_index);

                // Copy trajectories
                for (unsigned int u = 0; u < p.NumberTrajectoryPoints(); u++) {
                    h5fnal_trajectory_t trajectory;

                    trajectory.Vx   = p.Vx(u);
                    trajectory.Vy   = p.Vy(u);
                    trajectory.Vz   = p.Vz(u);
                    trajectory.T    = p.T(u);

                    trajectory.Px   = p.Px(u);
                    trajectory.Py   = p.Py(u);
                    trajectory.Pz   = p.Pz(u);
                    trajectory.E    = p.E(u);

                    new_trajectories++;
                    trajectories.push_back(trajectory);
                } /* end loop over trajectory points */

                /* Fix up trajectory dataset indices */
                if (new_trajectories > 0) {
                    particle.trajectory_start_index     = trajectories.size() - new_trajectories;
                    particle.trajectory_end_index       = trajectories.size() - 1;
                }
                else {
                    particle.trajectory_start_index     = -1;
                    particle.trajectory_end_index       = -1;
                }

                // Copy daughters
                for (int j = 0; j < p.NumberDaughters(); j++) {

                    h5fnal_daughter_t daughter;

                    daughter.track_id = p.Daughter(j);

                    new_daughters++;
                    daughters.push_back(daughter);
                } /* end loop over daughters */

                /* Fix up daughter dataset indices */
                if (new_daughters > 0) {
                    particle.daughter_start_index       = daughters.size() - new_daughters;
                    particle.daughter_end_index         = daughters.size() - 1;
                }
                else {
                    particle.daughter_start_index       = -1;
                    particle.daughter_end_index         = -1;
                }

                new_particles++;
                particles.push_back(particle);

            } /* end loop over particles */

            // Copy neutrino data. NOTE: must come after particles.
            if (t.NeutrinoSet()) {
                const simb::MCNeutrino& n = t.GetNeutrino();
                h5fnal_neutrino_t neutrino;
 
                neutrino.mode               = n.Mode();
                neutrino.interaction_type   = n.InteractionType();
                neutrino.ccnc               = n.CCNC();
                neutrino.target             = n.Target();
                neutrino.hit_nuc            = n.HitNuc();
                neutrino.hit_quark          = n.HitQuark();
                neutrino.w                  = n.W();
                neutrino.x                  = n.X();
                neutrino.y                  = n.Y();
                neutrino.q_sqr              = n.QSqr();
 
                neutrinos.push_back(neutrino);
            }

            // Fix up neutrino index
            if (t.NeutrinoSet())
                truth.neutrino_index            = neutrinos.size() - 1;
            else
                truth.neutrino_index            = -1;   // No neutrino


            /* Fix up particle dataset indices */
            if (new_particles > 0) {
                truth.particle_start_index      = particles.size() - new_particles;
                truth.particle_end_index        = particles.size() - 1;
            }
            else {
                truth.particle_start_index      = -1;
                truth.particle_end_index        = -1;
            }

            truths.push_back(truth);

        } /* end loop over truths */

        truth_data.n_truths         = truths.size();
        truth_data.truths           = &truths[0];
        truth_data.n_trajectories   = trajectories.size();
        truth_data.trajectories     = &trajectories[0];
        truth_data.n_daughters      = daughters.size();
        truth_data.daughters        = &daughters[0];
        truth_data.n_particles      = particles.size();
        truth_data.particles        = &particles[0];
        truth_data.n_neutrinos      = neutrinos.size();
        truth_data.neutrinos        = &neutrinos[0];

        // Write the flattened Vector of MCTruth to the HDF5 data product
        if (h5fnal_append_truths(h5vtruth, &truth_data) < 0)
            H5FNAL_PROGRAM_ERROR("could not write truths to the HDF5 data product");

        /* Close the event and HDF5 data product */
        if (h5fnal_close_v_mc_truth(h5vtruth) < 0)
            H5FNAL_PROGRAM_ERROR("could not close HDF5 data product");
        if (h5fnal_close_event(event_id) < 0)
            H5FNAL_PROGRAM_ERROR("could not close event");

    } /* end of loop over events */

    /* Clean up */
    if (H5Pclose(fapl_id) < 0)
        H5FNAL_HDF5_ERROR;
    if (h5fnal_close_run(master_id) < 0)
        H5FNAL_PROGRAM_ERROR("could not close master run container")
    // The run and sub-run will still be open after the last loop iteration.
    if (h5fnal_close_run(run_id) < 0)
        H5FNAL_PROGRAM_ERROR("could not close run")
    if (h5fnal_close_run(subrun_id) < 0)
        H5FNAL_PROGRAM_ERROR("could not close sub-run")
    if (close_string_dictionary(dict) < 0)
        H5FNAL_PROGRAM_ERROR("could not string dictionary")
    if (H5Fclose(fid) < 0)
        H5FNAL_HDF5_ERROR;

    free(dict);
    free(h5vtruth);

    cout << "Wrote " << totalTruths << " TOTAL truths to the HDF5 file." << endl;
    std::cout << "*** SUCCESS ***\n";
    exit(EXIT_SUCCESS);

error:

    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        h5fnal_close_run(run_id);
        h5fnal_close_run(subrun_id);
        h5fnal_close_event(event_id);
        h5fnal_close_run(master_id);
        if (h5vtruth)
            h5fnal_close_v_mc_truth(h5vtruth);
        if (dict)
            close_string_dictionary(dict);
        H5Fclose(fid);
    } H5E_END_TRY;

    free(dict);
    free(h5vtruth);

    std::cout << "*** FAILURE ***\n";
    exit(EXIT_FAILURE);
}
