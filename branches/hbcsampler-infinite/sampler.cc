/*
 * Copyright (C) 2012 Jiri Materna <xmaterna@fi.muni.cz>
 * Licensed under the GNU GPLv3 - http://www.gnu.org/licenses/gpl-3.0.html
 *
 */

#include <iostream>
#include <sstream>
#include <math.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <dirent.h>
#include "sampler.h"

#define BOUNDPROB(x) (((x)<-300)?(-300):((((x)>300)?(300):(x))))

void Sampler_t::resample_post_phi(void) {
    for (unsigned int u = 1; u <= U; ++u) {
        for (unsigned int f = 1; f<=F; ++f) {
            post_phi[u-1][f-1] = 0;
        }
        post_phi[u-1][F] = 0;
        for (unsigned int t = 1; t <= w[u-1].size(); ++t) {
            post_phi[u-1][F]++;
            post_phi[u-1][frames[u-1][t-1]-1]++;
        }
    }
}

void Sampler_t::resample_post_theta(void) {
    for (unsigned int r = 1; r <= R; ++r) {
        for (unsigned int v = 1; v <= V; ++v) {
            post_theta[r-1][v-1] = 0;
        }
        post_theta[r-1][V] = 0;
        for (unsigned int u = 1; u <= U; ++u) {
            for (unsigned int t = 1; t <= w[u - 1].size(); ++t) {
                for (unsigned int s = 1; s <= S; ++s) {
                    post_theta[r-1][V] += (r == roles[frames[u-1][t-1]-1][s-1]) ? 1 : 0;
                    post_theta[r-1][w[u-1][t-1][s-1]-1] += 
                        (r == roles[frames[u-1][t-1]-1][s-1]) ? 1 : 0;
                }
            }
        }
    }
}

void Sampler_t::resample_post_omega(void) {
    for (unsigned int f = 1; f <= F; ++f) {
        for (unsigned int s = 1; s <= S; ++s) {
             post_omega[roles[f-1][s-1]-1] = 0;
        }
    }
    post_omega[R] = 0;
    for (unsigned int f = 1; f <= F; ++f) {
        for (unsigned int s = 1; s <= S; ++s) {
            post_omega[roles[f-1][s-1]-1]++;
            post_omega[R]++;
        }
    }
}

void Sampler_t::resample_tau(void) {
    vector<unsigned int> idmap;
    unsigned int id = 0;
    double* fc = (double*) malloc(sizeof(double) * (used_frames.size() + 1));
    double* dirs = (double*) malloc(sizeof(double) * (used_frames.size() + 2));
    tables = 0;
    for(set<unsigned int>::const_iterator it = used_frames.begin(); it != used_frames.end(); ++it) {
        idmap.push_back(*it);
        fc[id] = 0;
        for (unsigned int u=1; u<=U; ++u) {
            if (post_phi[u-1][*it-1] > 1) {
                fc[id] += dist->sampleAntoniak(alpha * tau[*it], post_phi[u-1][*it-1]);
            } else {
                fc[id] += post_phi[u-1][*it-1];
            }
        }
        tables += fc[id];
        id++;
    }    
    fc[used_frames.size()] = delta;
    dist->sampleDirichlet(dirs, fc, used_frames.size() + 1);
    for (unsigned int i=0; i<used_frames.size(); ++i) {
        tau[idmap[i]] = dirs[i];
    }
    tau[0] = dirs[used_frames.size()]; 
    free(fc);
    free(dirs);
}


void Sampler_t::resample_frames(void) {
    for (int u = 1; u <= (signed int) U; ++u) {
        for (unsigned int t = 1; t <= w[u-1].size(); ++t) {

            map<unsigned short int, double> post_frames;

            //remove old values
            for (unsigned int s = 1; s <= S; ++s) {
                post_theta[roles[frames[u-1][t-1]-1][s-1]-1][V]--;
                post_theta[roles[frames[u-1][t-1]-1][s-1]-1][w[u-1][t-1][s-1]-1]--;
                fc_fsw[frames[u-1][t-1]-1][s-1][w[u-1][t-1][s-1]-1]--;
            }
            post_phi[u-1][F]--;
            post_phi[u-1][frames[u-1][t-1]-1]--;
            fc_f[frames[u-1][t-1]-1]--;

            //compute frame distribution
            for (set<unsigned int>::const_iterator fit = used_frames.begin(); fit != used_frames.end(); 
                    ++fit) {
                double prod = 0;
                for (unsigned int s = 1; s <= S; ++s) {
                    prod += BOUNDPROB(
                            log(post_theta[roles[*fit-1][s-1]-1][w[u-1][t-1][s-1]-1] + 
                            beta[roles[*fit-1][s-1]-1][w[u-1][t-1][s-1]-1]) -
                            log(post_theta[roles[*fit-1][s-1]-1][V] + beta[roles[*fit-1][s-1]-1][V])
                            ); 
                }
                post_frames[*fit] = prod + BOUNDPROB(log(post_phi[u-1][*fit-1] + alpha));
            }

            //sample frame
            dist->normalizeLogMult(post_frames);
            frames[u-1][t-1] = dist->sampleMultinomial(post_frames);

            //update new values
            for (unsigned int s=1; s<=S; ++s) {
                post_theta[roles[frames[u-1][t-1]-1][s-1]-1][V]++;
                post_theta[roles[frames[u-1][t-1]-1][s-1]-1][w[u-1][t-1][s-1]-1]++;
                fc_fsw[frames[u-1][t-1]-1][s-1][w[u-1][t-1][s-1]-1]++;
            }
            post_phi[u-1][F]++;
            post_phi[u-1][frames[u-1][t-1]-1]++;
            fc_f[frames[u-1][t-1]-1]++;
        }
    }
}

void Sampler_t::resample_frames_inf(void) {
    for (int u = 1; u <= (signed int) U; ++u) {
        for (unsigned int t = 1; t <= w[u-1].size(); ++t) {

            map<unsigned short int, double> post_frames;
            bool tau_needs_resampling = false;

            //remove old values
            if (frames[u-1][t-1] > 0) { //already sampled?
                for (unsigned int s = 1; s <= S; ++s) {
                    post_theta[roles[frames[u-1][t-1]-1][s-1]-1][V]--;
                    post_theta[roles[frames[u-1][t-1]-1][s-1]-1][w[u-1][t-1][s-1]-1]--;
                    fc_fsw[frames[u-1][t-1]-1][s-1][w[u-1][t-1][s-1]-1]--;

                }
                post_phi[u-1][F]--;
                post_phi[u-1][frames[u-1][t-1]-1]--;
                fc_f[frames[u-1][t-1]-1]--;
            }

            //compute frame distribution
            for (set<unsigned int>::const_iterator fit = used_frames.begin(); fit !=used_frames.end(); 
                    ++fit) {
                //probability of used frames
                double prod = 0;
                for (unsigned int s = 1; s <= S; ++s) {
                    prod += BOUNDPROB(
                            log(post_theta[roles[*fit-1][s-1]-1][w[u-1][t-1][s-1]-1] +
                            beta[roles[*fit-1][s-1]-1][w[u-1][t-1][s-1]-1]) -
                            log(post_theta[roles[*fit-1][s-1]-1][V] + beta[roles[*fit-1][s-1]-1][V])
                            );
                }
                post_frames[*fit] = prod + BOUNDPROB(log(post_phi[u-1][*fit-1] + alpha*tau[*fit]));
            }
            //sample new frame
            vector<unsigned int> frame(S, 0);
            if (sample_new_frame(frame, w[u-1][t-1])) {
                double prod = BOUNDPROB(log(alpha*tau[0]));
                for (unsigned int s = 1; s <= S; ++s) {
                    prod -= BOUNDPROB(log(V));
                }
                post_frames[F+1] = prod;
            }

            //sample frame id
            dist->normalizeLogMult(post_frames);
            unsigned int newFrame = dist->sampleMultinomial(post_frames);
            
            //free unused frames and roles
            if (frames[u-1][t-1] != newFrame && frames[u-1][t-1] != 0 && fc_f[frames[u-1][t-1]-1] == 0) {
                frameSet.remove(frameSet.makeKey(roles[frames[u-1][t-1]-1]));
                for (unsigned int s=1; s<=S; ++s) {
                    post_omega[roles[frames[u-1][t-1]-1][s-1]-1]--;
                    post_omega[R]--;
                    if (post_omega[roles[frames[u-1][t-1]-1][s-1]-1] == 0 && infinite_R) {
                        unused_roles.insert(roles[frames[u-1][t-1]-1][s-1]);
                        used_roles.erase(roles[frames[u-1][t-1]-1][s-1]);
                    }
                }
                unused_frames.insert(frames[u-1][t-1]);
                used_frames.erase(frames[u-1][t-1]);
                tau_needs_resampling = true;
                tau.erase(frames[u-1][t-1]);
            }

            //create a new frame if required
            if (newFrame == F + 1) {
                if (frames[u-1][t-1]==0 && u==1 && t==1) {
                    used_frames.erase(1);
                    unused_frames.insert(1);
                    post_omega[0] -= 2;
                }
                frames[u-1][t-1] = createNewFrame(frame);
                tau[frames[u-1][t-1]] = tau[0];
                tau_needs_resampling = true;
            } else {
                frames[u-1][t-1] = newFrame;
                
                //remove possibly new roles of the unused new frame
                for (unsigned int s=1; s<=S; ++s) {
                    if (frame[s-1] != 0 && post_omega[frame[s-1]-1] == 0) {
                        used_roles.erase(frame[s-1]);
                        unused_roles.insert(frame[s-1]);
                    }
                }
            }
            
            //update new values
            for (unsigned int s=1; s<=S; ++s) {
                post_theta[roles[frames[u-1][t-1]-1][s-1]-1][V]++;
                post_theta[roles[frames[u-1][t-1]-1][s-1]-1][w[u-1][t-1][s-1]-1]++;
                fc_fsw[frames[u-1][t-1]-1][s-1][w[u-1][t-1][s-1]-1]++;
            }
            post_phi[u-1][F]++;
            post_phi[u-1][frames[u-1][t-1]-1]++;
            fc_f[frames[u-1][t-1]-1]++;
                    
            if(tau_needs_resampling) resample_tau();

        }
    }
}

void Sampler_t::resample_roles(void) {
      for (set<unsigned int>::const_iterator fit = used_frames.begin(); fit!=used_frames.end(); ++fit) {
        for (unsigned int s = 1; s <= S; ++s) {
            map<unsigned short int, double> post_roles;

            post_theta[roles[*fit-1][s-1]-1][V] -= fc_f[*fit-1];
            for(unsigned int v=1; v<=V; ++v) {
                post_theta[roles[*fit-1][s-1]-1][v-1] -= fc_fsw[*fit-1][s-1][v-1];
            }

            post_omega[roles[*fit-1][s-1]-1]--;
            post_omega[R]--;
           
            FrameKey_t oldFrame; 
            oldFrame = frameSet.makeKey(roles[*fit-1]);

            for (set<unsigned int>::const_iterator rit = used_roles.begin(); rit != used_roles.end();
                    ++rit) {
                FrameKey_t newFrame = frameSet.makeKey(roles[*fit-1], s, *rit);
                bool inside = frameSet.inside(newFrame);
                if (newFrame == oldFrame || !inside) {
                    double prod = 0;
                    for (unsigned int v = 1; v<=V; ++v) {
                        prod += fc_fsw[*fit-1][s-1][v-1]*BOUNDPROB(                                   
                            log(post_theta[*rit-1][v-1] +
                            beta[*rit-1][v-1]) -
                            log(post_theta[*rit-1][V] + beta[*rit-1][V])
                            );
                    }
                    post_roles[*rit] = prod + BOUNDPROB(log(post_omega[*rit-1] + gamma));
                }
            }

            //sample role
            dist->normalizeLogMult(post_roles);
            roles[*fit-1][s-1] = dist->sampleMultinomial(post_roles);
        
            frameSet.remove(oldFrame);
            frameSet.insert(frameSet.makeKey(roles[*fit-1]));

            post_omega[roles[*fit-1][s-1]-1]++;
            post_omega[R]++;
            post_theta[roles[*fit-1][s-1]-1][V] += fc_f[*fit-1];
            for(unsigned int v=1; v<=V; ++v) {
                post_theta[roles[*fit-1][s-1]-1][v-1] += fc_fsw[*fit-1][s-1][v-1];
            }
        }
    }
}

void Sampler_t::resample_roles_inf(void) {

    for (set<unsigned int>::const_iterator fit = used_frames.begin(); fit!=used_frames.end(); ++fit) {
        for (unsigned int s = 1; s <= S; ++s) {
            map<unsigned short int, double> post_roles;

            post_theta[roles[*fit-1][s-1]-1][V] -= fc_f[*fit-1];
            for(unsigned int v=1; v<=V; ++v) {
                post_theta[roles[*fit-1][s-1]-1][v-1] -= fc_fsw[*fit-1][s-1][v-1];
            }

            post_omega[roles[*fit-1][s-1]-1]--;
            post_omega[R]--;

            FrameKey_t oldFrame; 
            oldFrame = frameSet.makeKey(roles[*fit-1]);
            
            for (set<unsigned int>::const_iterator rit = used_roles.begin(); rit != used_roles.end(); ++rit) {
                FrameKey_t newFrame = frameSet.makeKey(roles[*fit-1], s, *rit);
                bool inside = frameSet.inside(newFrame);

                //probability of used roles
                if (newFrame == oldFrame || !inside) {
                    double prod = 0;
                    for (unsigned int v = 1; v<=V; ++v) {
                        prod += fc_fsw[*fit-1][s-1][v-1]*BOUNDPROB(
                            log(post_theta[*rit-1][v-1] +
                            beta[*rit-1][v-1]) -
                            log(post_theta[*rit-1][V] + beta[*rit-1][V])
                            );
                    }
                    post_roles[*rit] = prod + BOUNDPROB(log(post_omega[*rit-1] + gamma));
                }
            }

            //probability of a new role
            double prod = 0;
            for (unsigned int v = 1; v<=V; ++v) {
                prod -= fc_fsw[*fit-1][s-1][v-1]*log(V);
            }
            post_roles[R + 1] = prod + BOUNDPROB(log(gamma));
            

            //Sample role id
            dist->normalizeLogMult(post_roles);
            unsigned int newRole = dist->sampleMultinomial(post_roles);

            //free unused role numbers
            if (roles[*fit-1][s-1] != newRole && post_omega[roles[*fit-1][s-1]-1] == 0) {
                unused_roles.insert(roles[*fit-1][s-1]);
                used_roles.erase(roles[*fit-1][s-1]);
            }
            //create a new role if required
            if (newRole == R + 1) {
                roles[*fit-1][s-1] = createNewRole();
            } else {
                roles[*fit-1][s-1] = newRole;
            }

            frameSet.remove(oldFrame);
            frameSet.insert(frameSet.makeKey(roles[*fit-1]));

            post_omega[roles[*fit-1][s-1]-1]++;
            post_omega[R]++;
            post_theta[roles[*fit-1][s-1]-1][V] += fc_f[*fit-1];
            for(unsigned int v=1; v<=V; ++v) {
                post_theta[roles[*fit-1][s-1]-1][v-1] += fc_fsw[*fit-1][s-1][v-1];
            }
        }
    }
}

void Sampler_t::resample_hypers(unsigned int iters) {

    //sample beta
    for (set<unsigned int>::const_iterator rit = used_roles.begin(); rit!=used_roles.end(); ++rit) {
        for (unsigned int v=1; v<=V; ++v) {
            for (unsigned int iter = 0; iter < iters; ++iter) {
                double oldBeta = beta[*rit-1][v-1];
                beta[*rit-1][v-1] = max(
                    beta[*rit-1][v-1]*
                    (dist->digamma(post_theta[*rit-1][v-1]+beta[*rit-1][v-1])-
                     dist->digamma(beta[*rit-1][v-1]))/
                    (dist->digamma(post_theta[*rit-1][V]+beta[*rit-1][V])-
                     dist->digamma(beta[*rit-1][V])),
                    beta0);
                if (post_theta[*rit-1][V] == 0) beta[*rit-1][v-1] = beta0;
                beta[*rit-1][V] += beta[*rit-1][v-1] - oldBeta;
            }

        }
    }

    if (infinite_F) {
        double bdelta = 1.0;
        double adelta = 1.0;
        double aalpha = 1.0;
        double balpha = 1.0;

        //sample delta

        double eta = dist->sampleBeta(delta + 1, tables);
        double pi = adelta + used_frames.size() - 1;
        double rate = 1.0 / bdelta - log(eta);
        pi = pi / (pi + rate * tables);
    
        unsigned int cc = dist->sampleBernoulli(pi);
        if (cc == 1) {
            delta = dist->sampleGamma(adelta + used_frames.size(), 1.0 / rate);
        } else {
            delta = dist->sampleGamma(adelta + used_frames.size() - 1, 1.0 / rate);
        }
    
        //sample alpha
        for (unsigned int i=0; i<iters; i++) {
            double sum_log_w = 0.0;
            double sum_s = 0.0;
            for (unsigned int u=1; u<=U; ++u) {
                sum_log_w += log(dist->sampleBeta(alpha + 1, w[u-1].size()));
                sum_s += (double)dist->sampleBernoulli(w[u-1].size() / (w[u-1].size() + alpha));
            }
            rate = 1.0 / balpha - sum_log_w;
            alpha = dist->sampleGamma(aalpha + tables - sum_s, 1.0 / rate);

        }
    }
}

void Sampler_t::sample(void) {
    cout << " frames..." << flush;
    if (infinite_F) {
        resample_frames_inf();
    } else {
        resample_frames();
    }
    cout << "roles..." << flush;
    if (infinite_R) {    
        resample_roles_inf();
    } else {
        resample_roles();
    }
    if (infinite_F) {
        cout << "tau..." << flush;
        resample_tau();
    }
    
}

bool Sampler_t::sampleAll(string outputDir, unsigned int iters, bool allSamples) {

    if (outputDir.at(outputDir.size()-1) != '/') outputDir += "/";
    int status = mkdir(outputDir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if(iters < startIter) {
        cout << "All iterations already performed." << endl;
        return true;
    }   

    //TODO check outputDir
    if (status != 0 && errno != EEXIST) {
        cout << "Cannot create directory '" << outputDir << "'\n";
        return false;
    }

    //remove old files
    DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(outputDir.c_str())) == NULL) {
        cout << "Error(" << errno << ") opening " << outputDir << endl;
        return false;
    }

    while ((dirp = readdir(dp)) != NULL) {
        string fname = string(dirp->d_name);
        if (fname.length() > 5 && 
            fname.substr(fname.length()-5, 5).compare(".smpl") == 0) {
            if (remove((outputDir + fname).c_str()) != 0) {
                cout << "Cannot remove file " << outputDir + fname << endl;
                return false;
            }
        }
    }
    closedir(dp);

    for (unsigned int i = startIter; i < iters+1; ++i) {
        cout << "Iteration no. " << i << ":";
        cout << flush;
        sample();
        if (i>=minHyperIter) {
            cout << "hyperparameters..." << flush;
            resample_hypers(20);
        }
        cout << "perplexity..." << flush;
        double p = perplexity();
        cout << " (" << used_frames.size() << " frames, " 
             << used_roles.size() << " roles).";
        cout << " Perplexity: " << p;
        cout << endl;


        stringstream ss;
        if (allSamples) {
            ss << i << "-";
        }

        if(infinite_F || infinite_R) {
            //cout << "...packing frames and roles.";
            pack_FR();
            //cout << endl;
        }

        if (!dump(outputDir + ss.str())) {
            return false;
        }
        if (!writeLog(outputDir, i, iters)) {
            return false;
        }
    }
    
    return true;
}


unsigned int Sampler_t::createNewRole(void) {
    unsigned int role;
    if (!unused_roles.empty()) {
        set<unsigned int>::iterator it = unused_roles.begin();
        role = *it;
        unused_roles.erase(it);
    } else {
        R++;
        role = R;
        beta.push_back(vector<double>(V + 1, beta0));
        post_theta.push_back(vector<double>(V + 1, 0));
        post_omega.push_back(0);
        post_omega[R] = post_omega[R-1];
        post_omega[R-1] = 0;
    }
    beta[role-1][V] = V*beta0;
    used_roles.insert(role);
    return role;
}


bool Sampler_t::sample_new_frame(vector<unsigned int> &frame, vector<unsigned int> &pos) {
    for (unsigned int s=1; s<=S; ++s) {

        map<unsigned short int, double> post_roles;
        for (set<unsigned int>::const_iterator rit=used_roles.begin(); rit!=used_roles.end();
                ++rit) {
            post_roles[*rit] = BOUNDPROB(
                log(post_theta[*rit-1][pos[s-1]-1] + beta[*rit-1][pos[s-1]-1]) -
                log(post_theta[*rit-1][V] + beta[*rit-1][V]) +
                log(gamma + post_omega[*rit-1])
                );
        }
        post_roles[R + 1] = log(gamma) - log(V);

        //sample role id
        dist->normalizeLogMult(post_roles);
        unsigned int newRole = dist->sampleMultinomial(post_roles);
        if (newRole == R + 1) {
            frame[s-1] = createNewRole();
        } else {
            frame[s-1] = newRole;
        }
    }

    if(frameSet.inside(frameSet.makeKey(frame))) 
        return false;
    else
        return true;
}


unsigned int Sampler_t::createNewFrame(vector<unsigned int> &frame) {
    unsigned int frameId;
    if (!unused_frames.empty()) {
        set<unsigned int>::iterator it = unused_frames.begin();
        frameId = *it;
        unused_frames.erase(it);
    } else {
        F++;
        frameId = F;
        for (unsigned int u = 1; u <= U; ++u) {
            post_phi[u-1].push_back(0);
            post_phi[u-1][F] = post_phi[u-1][F-1];
            post_phi[u-1][F-1] = 0;
        }
        roles.push_back(vector<unsigned int>(S, 0));
        fc_f.push_back(0);
        fc_fsw.push_back(vector<vector<unsigned int> >(S,vector<unsigned int>(V, 0)));
    }

    used_frames.insert(frameId);

    for (unsigned int s = 1; s <= S; ++s ) {
        roles[frameId-1][s-1] = frame[s-1];
        post_omega[roles[frameId-1][s-1]-1]++;
        post_omega[R]++;
    }

    FrameKey_t fk = frameSet.makeKey(roles[frameId-1]);
    frameSet.insert(fk);
    
    return frameId;
}


double Sampler_t::perplexity(void) {
    double loglik = 0;
    int words = 0;
    
    for (unsigned int u=1; u<=U; ++u) {
        for (unsigned int t=1; t <= w[u-1].size(); ++t) {
           unsigned int f = frames[u-1][t-1];
           loglik += BOUNDPROB(log(post_phi[u-1][f-1]) -
                     log(post_phi[u-1][F]));
           for (unsigned int s=1; s<=S; ++s) {
                unsigned int r = roles[f-1][s-1];
                words++;
                loglik += BOUNDPROB(log(post_theta[r-1][w[u-1][t-1][s-1]-1]) -
                          log(post_theta[r-1][V]));
            }
        }
    }
    return exp(-loglik/words);
}


void Sampler_t::pack_FR(void) {

    //pack frames and roles    
    map<unsigned int, unsigned int> tmp_F, tmp_R;
    unsigned int f=0, r=0;
    for (set<unsigned int>::const_iterator it=used_frames.begin();
            it!= used_frames.end(); ++it) {
        tmp_F[*it] = ++f;
    }
    for (set<unsigned int>::const_iterator it=used_roles.begin();
            it!= used_roles.end(); ++it) {
        tmp_R[*it] = ++r;
    }
    for(unsigned int u=1; u<=U; ++u) {
        for (unsigned int t = 1; t <= w[u-1].size(); ++t) {
            frames[u-1][t-1] = tmp_F[frames[u-1][t-1]];
        }
    }
    vector<vector<unsigned int> > tmp_roles;
    fc_f.clear();
    fc_fsw.clear();
    for (unsigned int f=1; f<=used_frames.size(); ++f) {
        tmp_roles.push_back(vector<unsigned int>(S, 0));
        fc_f.push_back(0);
        fc_fsw.push_back(vector<vector<unsigned int> >(S,vector<unsigned int>(V, 0)));
    }

    map<unsigned int, double> tmp_tau;
    tmp_tau[0] = F;
    frameSet.clear();
    for (set<unsigned int>::const_iterator it = used_frames.begin(); it != used_frames.end(); ++it) {
        tmp_tau[tmp_F[*it-1]] = tau[*it];
        for (unsigned int s=1; s<=S; ++s) {
            tmp_roles[tmp_F[*it]-1][s-1] = tmp_R[roles[*it-1][s-1]];
        }
        frameSet.insert(frameSet.makeKey(tmp_roles[tmp_F[*it]-1]));
    }
    tau = tmp_tau;

    roles = tmp_roles;
    
    for(unsigned int u=1; u<=U; ++u) {
        for (unsigned int t = 1; t <= w[u-1].size(); ++t) {
            fc_f[frames[u-1][t-1]-1]++;
            for (unsigned int s=1; s<=S; ++s) {
                fc_fsw[frames[u-1][t-1]-1][s-1][w[u-1][t-1][s-1]-1]++;
            }
        }
    }

    //reduce number of frames and roles
    F = used_frames.size();
    R = used_roles.size();
    unused_frames.clear();
    unused_roles.clear();
    used_frames.clear();
    used_roles.clear();
    for (unsigned int f=1; f<=F; ++f) used_frames.insert(f);
    for (unsigned int r=1; r<=R; ++r) used_roles.insert(r);

    resample_post_phi();
    resample_post_theta();
    resample_post_omega();

}

Sampler_t::~Sampler_t() {
    if (initialized) {
        delete dist;
    }
}
