//
// Created by joris on 17/11/2021.
//

#include <fstream>
#include "ibex_EvalMax.h"
#include "ibex_Timer.h"
#include "ibex_NoBisectableVariableException.h"

using namespace std;
namespace ibex {

    const double EvalMax::default_timeout = 20;
    const double EvalMax::default_goal_abs_prec = 1e-2;

//EvalMax::EvalMax(Function &f, int nx, int ny) {}
    EvalMax::EvalMax(ExtendedSystem &xy_sys, int nx, int ny, Ctc &ctc_xy) :
            trace(false), timeout(default_timeout),
            list_elem_max(0), nb_iter(0), prec_y(0),
            monitor(false), local_search_iter(0), xy_sys(xy_sys), goal_abs_prec(default_goal_abs_prec), ctc_xy(ctc_xy),
            bsc(new LargestFirst()), found_point(false), time(0), best_point_eval(xy_sys.box) {
//        TODO check
    }

    EvalMax::~EvalMax() = default;

    Interval EvalMax::eval(IntervalVector &X, double loup) {
        BoxProperties prop(this->xy_sys.box);
        this->eval(X, prop, loup);
    }

    Interval EvalMax::eval(IntervalVector &X, BoxProperties &prop, double loup) {
        delete_save_heap();

        found_point  = false;
        BxpMinMax *data_x;
        Cell *x_cell = new Cell(X);
        csp_actif = false; // TODO

//        if (csp_actif) data_x = dynamic_cast<BxpMinMax *>(prop[BxpMinMaxCsp::get_id(xy_sys)]);
//        else           data_x = dynamic_cast<BxpMinMax *>(prop[BxpMinMaxOpti::get_id(xy_sys)]);
        data_x = dynamic_cast<BxpMinMax *>(prop[BxpMinMax::get_id(*this)]);

        //std::cout <<"    DEB "<<data_x->fmax <<std::endl;
//                cout<<endl<<"*************************"<<endl;

        DoubleHeap<Cell> *y_heap = data_x->y_heap; // current cell
//        cout<<"get y_heap of size: "<<y_heap->size()<<endl;
//        cout<<"initial fmax: "<<data_x->fmax<<endl;
//        cout<<"box: "<<x_cell->box<<endl;

//        // Define the TimeOut of to compute the bounds of x_cell
        Timer timer;
        time = timer.get_time();
//
//        // ********** contract x_box with ctc_xy***************
//        //        if (!csp_actif) // no constraint if dealing with fa cst, better not use the useless contractor
//        //        {
        IntervalVector xy_box = xy_box_hull(x_cell->box);
        ctc_xy.contract(xy_box);

        if (xy_box.is_empty()) {
//                data_x->clear_fsbl_list(); // need to delete elements of fsbl_point_list since this branch is closed and they will not be needed later
//                delete x_cell;
            return false;
        } else {
            // contract the result on x
            x_cell->box &= xy_box.subvector(0,x_cell->box.size()-1);
        }
        if (local_search_iter>0)
        {
            double lower_bound_ls = local_search_process(x_cell->box, xy_box, loup);
            if (lower_bound_ls > data_x->fmax.ub()) {
                ibex_error("ibex_LightOptimMinMax: error, lb >ub from local search. Please report this bug."); // TODO ibex_LightOptimMinMax -> ibex_EvalMax
            }
//            cout<<"local optim return new lb: "<< lower_bound_ls<<endl;
            if (lower_bound_ls > loup) {
//                data_x->clear_fsbl_list(); // need to delete elements of fsbl_point_list since this branch is closed and they will not be needed later
//                delete x_cell;
                return false;
            }
            else {
                data_x->fmax &= Interval(lower_bound_ls, POS_INFINITY); // update lower bound on fmax
            }
        }


        //monitoring variables, used to track upper bound, lower bound, number of elem in y_heap and heap_save at each iteration
        std::vector<double> ub, lb, nbel, nbel_save;

        save_heap_ub = NEG_INFINITY;
//        cout<<"initial fmax: "<<data_x->fmax<<endl;



        // *********** loop ********************
        try {
            int current_iter = 1;
            while(!stop_crit_reached(current_iter, y_heap,data_x->fmax)) {
//                if (trace >= 3) cout << *y_heap << endl; TODO error


                found_point  = false;

                Cell* y_cell = y_heap->pop(); // we extract an element with critprob probability to take it according to the first crit
                current_iter++;
//                                                std::cout<<"current_iter: "<<current_iter<<std::endl;
                if ((list_elem_max != 0 && ((y_heap->size() + heap_save.size())>list_elem_max)) || (y_cell->box.size())<prec_y) { // continue to evaluate cells of y_heap without increasing size of list, need to do it else nothing happend if list already reached max size
                    bool res = handle_cell(x_cell, y_cell, loup);
                    if (!res) { // x_cell has been deleted
                        return false;
                    }
                }
                else
                {
                    try {
                        pair<Cell*,Cell*> subcells_pair = bsc->bisect(*y_cell);// bisect tmp_cell into 2 subcells
                        delete y_cell;
                        //                                                                std::cout<<"handle first cell in light optim"<<std::endl;
                        bool res = handle_cell(x_cell, subcells_pair.first, loup);
                        //                                                                std::cout<<"first cell handled"<<std::endl;
                        if (!res) { // x_cell has been deleted
                            delete subcells_pair.second;
                            //                                                                                std::cout <<"       OUT 1 "<<std::endl;
                            return false;
                        }
                        //                                                                std::cout<<"handle second cell in light optim"<<std::endl;
                        res = handle_cell( x_cell, subcells_pair.second,loup);
                        //                                                                std::cout<<"second cell handled"<<std::endl;
                        if (!res) { // x_cell has been deleted
                            //                                                                                std::cout <<"       OUT 2 "<<std::endl;
                            return false;
                        }

//                                if (found_point && !csp_actif) { // a feasible solution has been found
//                                    y_heap->contract(-(data_x->fmax.lb())); // to check

//                                }
                        // ** contract not yet
                        //if (found_point) { // a feasible solution has been found
                        //	y_heap->contract(-(data_x->fmax.lb())); // to check
                        //}
                    }
                    catch (NoBisectableVariableException& ) {
                        bool res = handle_cell(x_cell, y_cell,loup);
                        cout << " no bisectable caught" << endl;

//                                if (res) heap_save.push_back(y_cell);
                        if (!res) return false;

                    }
                }
                if (monitor)
                {
                    if (!y_heap->empty()) {
                        lb.push_back(data_x->fmax.lb());
                        auto top1_minmax = dynamic_cast<BxpMinMaxSub *>(y_heap->top1()->prop[BxpMinMaxSub::get_id(*this)]);
                        if (!top1_minmax)
                            ibex_error("[ibex_EvalMax] -- null ptr in eval: top1 element has no BxpMinMax.");
                        if (save_heap_ub < top1_minmax->pf.ub())
                            ub.push_back(top1_minmax->pf.ub());
                        else
                            ub.push_back(save_heap_ub);
                        nbel.push_back(y_heap->size());
                        nbel_save.push_back(heap_save.size());
                    }
                }
//                                                std::cout<<"nb iter: "<<current_iter<<std::endl;
                timer.check(time+timeout);

            }

        }
        catch (TimeOutException& ) { }

//        cout<<"visit all, y_heap size: "<<y_heap->size()<<endl;
        //force visit of all elements of the heap
//        cout<<"visit all = "<<visit_all<<endl;
        if (visit_all) {
//            cout<<"             y_heap detail: "<<endl;
            while (!y_heap->empty()) {
                Cell * y_cell = y_heap->pop();
//                cout<<"                 "<<y_cell->box<<endl;
                bool res = handle_cell(x_cell, y_cell, loup, true);
                if (!res) return false;
                if (monitor)
                {
                    if (!y_heap->empty()) {
                        lb.push_back(data_x->fmax.lb());
                        auto top1_minmax = dynamic_cast<BxpMinMaxSub *>(y_heap->top1()->prop[BxpMinMaxSub::get_id(*this)]);
                        if (!top1_minmax)
                            ibex_error("[ibex_EvalMax] -- null ptr in eval: top1 element has no BxpMinMaxSub.");
                        if (save_heap_ub < top1_minmax->pf.ub())
                            ub.push_back(top1_minmax->pf.ub());
                        else
                            ub.push_back(save_heap_ub);
                        nbel.push_back(y_heap->size());
                        nbel_save.push_back(heap_save.size());
                    }
                }
            }
        }
//        cout<<"all eval done"<<endl;
//        cout<<"out of loop"<<endl;
//                std::cout<<"light optim: out of loop"<<std::endl;
        /*
??    if (is_midp && !midp_hit) // midpoint x eval case: if no y found such as xy constraint respected, cannot keep the result
??        return Interval::EMPTY_SET;
         */
        // insert the y_box with a too small diameter (those which were stored in heap_save)
        //         std::cout<<"try to fill y_heap"<<std::endl;

//        if (visit_all == true) {
//            for(int i = 0; i<heap_save.size();i++) {
//                cout<<"         lightoptimMinMax: box: "<<heap_save.at(i)->box<<endl<<"                eval: "<<heap_save.at(i)->get<OptimData>().pf<<endl;
////                   <<"  mid eval: "<<xy_sys.goal->eval(heap_save.at(i)->box.mid())<<endl;
//            }
//        }
        fill_y_heap(*y_heap);
//        cout<<"heap filled"<<endl;

        //if (found_point) { // a feasible solution has been found
//        if (!csp_actif)
//            y_heap->contract(-(data_x->fmax.lb())); // to check
//            cout<<"contraction done"<<endl;
//        else
//            y_heap->contract(-loup); // in csp solve, remove all boxes y that satisfy the constraint for all
        //}
        //        std::cout<<"y_heap filled"<<std::endl;

        // ** contract y_heap now

        //        std::cout<<"found point pass"<<std::endl;


        if (y_heap->empty()) {
            cout <<"       OUT 3 "<< endl;
//            data_x->clear_fsbl_list(); // need to delete elements of fsbl_point_list since this branch is closed and they will not be needed later
//            delete x_cell;
//            if (csp_actif) // sic case: empty set y means that the set defined by the constraints g(x,y)<0 is empty, and therefore that the sic is respected over the empty set
//                return true;
//            else
//                return false; // minmax case: empty set means ????? suppose that x can be discarded????
            return csp_actif;
        }
//        cout<<"non empty heap"<<endl;

        // Update the lower and upper bound of "max f(x,y_heap)"
        auto top1_minmax = dynamic_cast<BxpMinMaxSub *>(y_heap->top1()->prop[BxpMinMaxSub::get_id(*this)]);
        if (!top1_minmax)
            ibex_error("[ibex_EvalMax] -- null ptr in eval: top1 element has no BxpMinMaxSub.");
        double new_fmax_ub = top1_minmax->pf.ub(); // get the upper bound of max f(x,y_heap)
        auto top2_minmax = dynamic_cast<BxpMinMaxSub *>(y_heap->top2()->prop[BxpMinMaxSub::get_id(*this)]);
        if (!top2_minmax)
            ibex_error("[ibex_EvalMax] -- null ptr in eval: top2 element has no BxpMinMaxSub.");
        double new_fmax_lb = top2_minmax->pf.lb(); // get the lower bound of max f(x,y_heap)

        //std::cout<<"new_fmax_ub: "<<new_fmax_ub<<std::endl<<"new_fmax_lb: "<<new_fmax_lb<<std::endl<<"fmax_lb (from found point): "<<data_x->fmax.lb()<<std::endl;

        if (new_fmax_ub< new_fmax_lb) {
            ibex_error("ibex_LightOptimMinMax: error, please report this bug.");
        }

//                std::cout<<"fmax ini: "<<data_x->fmax<<std::endl;
        data_x->fmax &= Interval(new_fmax_lb, new_fmax_ub);
//                std::cout<<"new_fmax_lb: "<<new_fmax_lb<<" new_fmax_ub: "<<new_fmax_ub<<std::endl;
//                std::cout<<"       fmax final: "<<data_x->fmax<<std::endl;
        //        if (data_x->fmax.lb()>100000){
        //            std::cout<<"Issue: fmax_lb > 100,000"<<std::endl;

        //        }
        cout << "loup: " << loup << endl;

//        if (csp_actif) { // if SIC, all boxes lower than  can be deleted since of no interest (verify the constraint)
//            y_heap->contract(loup);
//            if (y_heap->empty())
//                cout<<" y_heap empty after loup contraction"<<endl;
//        }

        if (data_x->fmax.is_empty() || data_x->fmax.lb() > loup) {
//                                std::cout <<"       OUT 4 "<<std::endl;
//                data_x->clear_fsbl_list(); // need to delete elements of fsbl_point_list since this branch is closed and they will not be needed later
//                delete x_cell;
            return false;
        }


        if (monitor)
        {
            lb.push_back(data_x->fmax.lb());
            ub.push_back(data_x->fmax.ub());
            nbel.push_back(y_heap->size());
            nbel_save.push_back(heap_save.size());
            export_monitor(&ub, &lb, &nbel, &nbel_save, x_cell->box); // TODO
        }
        best_point_eval = xy_box.mid();
        for(int i=0;i<xy_sys.box.size()-x_cell->box.size();i++) {
            best_point_eval[x_cell->box.size()+i] = y_heap->top1()->box[i].mid();
        }

//                std::cout <<"    FIN "<<data_x->fmax <<std::endl;
        return true;
    }


    bool EvalMax::handle_cell(Cell* x_cell, Cell* y_cell, double loup, bool no_stack) {

        IntervalVector xy_box = init_xy_box(x_cell->box, y_cell->box);
        // recuperer les data
        BxpMinMax* data_x;

        data_x = dynamic_cast<BxpMinMax *>(x_cell->prop[BxpMinMax::get_id(*this)]);
//        if (csp_actif)
//            data_x = dynamic_cast<BxpMinMax *>(x_cell->prop[BxpMinMaxCsp::get_id(*this)]);
//        else
//            data_x = dynamic_cast<BxpMinMax *>(x_cell->prop[BxpMinMaxOpti::get_id(*this)]);

        auto data_y = dynamic_cast<BxpMinMaxSub *>(y_cell->prop[BxpMinMaxSub::get_id(*this)]);
//        cout<<"xy_cell box: "<<xy_box<<" value of pu : "<<data_y->pu<<endl;


        if (data_y->pu != 1) { // Check constraints
            if (handle_constraint(data_y,xy_box, y_cell->box)) {
                delete y_cell;
                return true;
            }
//                cout<<"xy_box after contraction: "<<y_cell->box<<endl;
        }
        else {
            handle_cstfree(xy_box,y_cell);
        }
        /********************************************************************************/
        //mid point test (TO DO: replace with local optim to find a better point than midpoint)
        IntervalVector mid_y_box = get_feasible_point(x_cell,y_cell);
        if (!(mid_y_box.is_empty())) {

            // x y constraint respected for all x and mid(y), mid_y_box is a candidate for evaluation
//                Interval midres = xy_sys.goal->eval_baumann(mid_y_box);
            Interval midres = eval_all(xy_sys.goal, mid_y_box);
//                cout<<"midp: "<<mid_y_box<<", eval = "<<midres<<endl;
            if (loup < midres.lb()) {  // midres.lb()> best_max ->is now false since fmax.ub() unchanged in
                // there exists y such as constraint is respected and f(x,y)>best max, the max on x will be worst than the best known solution
                delete y_cell;
//                        data_x->clear_fsbl_list(); // need to delete elements of fsbl_point_list since this branch is closed and they will not be needed later
//                        delete x_cell;
                //                        std::cout <<"           OUT 6 mid="<<y_cell->box<<std::endl;
                return false; // no need to go further, x_box does not contain the solution
            }
            else if (midres.lb() > data_y->pf.lb()) { // found y such as xy constraint is respected
                // TODO	 to check		// il faut faire un contract de y_heap
                data_y->pf &= Interval(midres.lb(),POS_INFINITY);
                delete data_x->best_sol;
                data_x->best_sol = new IntervalVector(mid_y_box);
                if (data_x->fmax.lb() < midres.lb()) {
//                            data_x->best_sol = mid_y_box.mid();
//                            cout<<"found point: "<<midres<< "  at  "<<mid_y_box<<endl;
                    found_point = true;
                    data_x->fmax &= Interval(midres.lb(),POS_INFINITY);; // yes we found a feasible solution for all x
                }
            }
        }

        //************ part below add a contraction w.r.t f(x,y)<best_max, this part may not be efficient on every problem ******************************

        if (data_y->pu == 1) {
            IntervalVector xy_box_mem(xy_box);
//            cout<<"contraction in light solver, init box: "<<mid_y_box<<endl;
            xy_sys.goal->backward(Interval(NEG_INFINITY,loup),mid_y_box);
//                cout<<"             final box: "<<mid_y_box<<endl;

            if(xy_box.is_empty()) {
                delete y_cell;
                //                std::cout<<"delete x cell because empty contraction"<<std::endl;
                return false;
            }
            for (int i=x_cell->box.size(); i<xy_box.size(); i++) { // y contracted => E y, for all x f(x,y)>loup, x deleted
                if (xy_box[i] != xy_box_mem[i]) {
                    //                    std::cout<<"delete x cell because contraction on y"<<std::endl;
                    delete y_cell;
                    return false;
                }
            }

            // no contraction on y but contraction on  x: keep contraction on x
            // TODO to check normalement on peut propager la contraction sur le y et sur le x
            for (int k=0; k<x_cell->box.size(); k++) {
                x_cell->box[k] &= xy_box[k];
            }
        }
        //********************************************

        // Update the lower and upper bound on y
        //data_y->pf &= xy_sys.goal->eval_baumann(xy_box); // objective function evaluation
        if (eval_all(xy_sys.goal,xy_box).ub()>data_y->pf.ub()) {
            cout<<" ************************** CRITICAL ISSUE *******************"<<endl;
            cout<<" get worst upper bound, should not happen due to monotonicity of ifunc"<<endl;
            cout<<"***************************************************************"<<endl;
        }

//        cout<<"    dealing with box: "<<xy_box<<endl;
//        cout<<"    previous value of pf:  "<<data_y->pf<<endl;
        data_y->pf &= eval_all(xy_sys.goal,xy_box);
//        cout<<"    eval res: "<<eval_all(xy_sys.goal,xy_box)<<endl;
//        cout<<"    new value of pf: "<<data_y->pf<<endl;
        if (data_y->pf.is_empty() || data_x->fmax.lb() > data_y->pf.ub()) {  // y_box cannot contains max f(x,y)
            delete y_cell;
            return true;
        }

        // check if it is possible to find a better solution than those already found on x
        if ((data_y->pf.lb() > loup) && (data_y->pu == 1)) {
            // box verified condition and eval is above best max, x box does not contain the solution
            // I think this case was already check with the mid-point.
            delete y_cell;
//                data_x->clear_fsbl_list(); // need to delete elements of fsbl_point_list since this branch is closed and they will not be needed later
//                delete x_cell;
            //std::cout <<"           OUT 7 "<<std::endl;
            return false; // no need to go further, x_box does not contains the solution
        }

        //*************************************************
        // store y_cell
        if (y_cell->box.max_diam() < prec_y) {
            //            std::cout<<"y_cell pushed in heap_save, box: "<<y_cell->box<<" pf: "<<data_y->pf<<" pu: "<<data_y->pu<<std::endl;
            save_heap_ub = save_heap_ub<data_y->pf.ub()? data_y->pf.ub() : save_heap_ub;
            heap_save.push_back(y_cell);
        }
        else {
            if (!no_stack) {
//                check_already_in(y_cell,data_x->y_heap);
                data_x->y_heap->push(y_cell);
            }
            else
                heap_save.push_back(y_cell);
        }
        return true;
    }

    void EvalMax::delete_save_heap() {
        while (!heap_save.empty()) {
            delete heap_save.back();
            heap_save.pop_back();
        }
    }

    IntervalVector EvalMax::xy_box_hull(const IntervalVector& x_box) {
        IntervalVector res(xy_sys.nb_var);
        for (int k=0; k<x_box.size(); k++)
            res[k] = x_box[k];
        for (int k = x_box.size(); k<xy_sys.nb_var; k++) // update current y box in the xy_box
            res[k] = xy_sys.box[k];
        return res;
    }



    double EvalMax::local_search_process(const IntervalVector& x_box, const IntervalVector & xy_box, double loup) {
        Interval res = Interval(Interval::ALL_REALS);
        IntervalVector xy_box_eval(xy_box);
        for (int i = 0; i<local_search_iter; i++) {
            Vector xy_rand = xy_box.random(); // pick a random x
            // set_y_sol(xy_rand); // TODO does nothing
            Vector final_point = xy_box.mid();

//        for(int k=0;k<x_box.size();k++)
//            optim_box[k] = xy_rand[k];
            local_solver->set_box(xy_box);
//        cout<<"             box: "<< xy_box<<" random start: "<<xy_rand<<endl;
            local_solver->minimize(xy_rand,final_point,1e-3); // maximizes
//        cout<<"             local solution: "<<xy_max<<endl<<"     goal value: "<<eval_all(xy_sys.goal,xy_max)<<endl;
            for (int k =x_box.size(); k<xy_box.size(); k++) // update current y box in the xy_box for evaluation at a point y given by local optim
                xy_box_eval[k] = final_point[k];
            if (check_constraints(xy_box) == 2) // y_max must respect constraints
            {
//            Interval eval = xy_sys.goal->eval_baumann(xy_box_eval); // eval objective function at (x_box,y_max)
                Interval eval = eval_all(xy_sys.goal, xy_box_eval);
//            cout<<"local optim solution: "<<xy_box_eval<<" objectif eval at solution: "<<eval<<endl;
                res &= Interval(eval.lb(), POS_INFINITY); // update greatest lower bound


//            cout<<"res = "<<res<<endl;
            }
            if (res.lb() > loup) // infeasible problem, break
                break;
        }
        return res.lb();
//    return 0;
    }



    bool EvalMax::stop_crit_reached(int current_iter, DoubleHeap<Cell>* y_heap, const Interval& fmax) const {
        if (nb_iter !=0 && current_iter >= nb_iter) // nb_iter ==  0 implies minimum precision required (may be mid point x case)
        {
//            cout<<"Stop light solver: nb_iter max reached"<<endl;
            return true;
        }
//        if(list_elem_max != 0 && y_heap->size() + heap_save.size()>list_elem_max) {
////            cout<<"Stop light solver: list reached max elem"<<endl;
//		return true;
//        }
        if (y_heap->size() == 0) {
//            cout<<"Stop light solver: empty buffer"<<endl;
            return true;
        }

        auto top1_minmax = dynamic_cast<BxpMinMaxSub *>(y_heap->top1()->prop[BxpMinMaxSub::get_id(*this)]);
        if (!top1_minmax)
            ibex_error("[ibex_EvalMax] -- null ptr in stop_crit_reached: top1 element has no BxpMinMaxSub.");
        if (csp_actif && (top1_minmax->pf.ub() < 0)) { // for all y constraint respected, stop
//            cout<<"Stop light solver: Csp case, upper bound lower than 0"<<endl;
            return true;
        }
//        if (fmax.diam()<goal_abs_prec) { //desired precision on global maximum enclosure reached
//            return true;
//        }
        return false;
    }


    void EvalMax::fill_y_heap(DoubleHeap<Cell>& y_heap) {
        Cell* tmp_cell;
        //        int nb_pt(0);
        while (!heap_save.empty()) { // push all boxes of heap_save in y_heap
            //            std::cout<<"try to get last elem of heap_save"<<std::endl;
            tmp_cell = heap_save.back();
            //                if(tmp_cell->box.max_diam()<1.e-14)
            //                    nb_pt++;
            //                OptimData  *data_y = &(tmp_cell->get<OptimData>());
            //                std::cout<<""
            //                std::cout<<"try to push last elem of heap_save,box: "<<tmp_cell->box<<std::endl;
            y_heap.push(tmp_cell);
            //                std::cout<<"try to pop last elem of heap_save"<<std::endl;
            heap_save.pop_back();
        }
        //        if(nb_pt>1) {
        //            std::cout<<"CRITICAL ISSUE: more than 1 point in heap"<<std::endl;
        //        }
//	heap_save.clear();
    }

    IntervalVector EvalMax::init_xy_box(const IntervalVector& x_box, const IntervalVector& y_box) {
        IntervalVector res(x_box.size()+y_box.size());
        for (int k=0; k<x_box.size(); k++)
            res[k] = x_box[k];
        for (int k = 0; k<y_box.size(); k++) // update current y box in the xy_box
            res[k+x_box.size()] = y_box[k];
        return res;
    }

    bool EvalMax::handle_constraint(BxpMinMaxSub* data_y, IntervalVector& xy_box, IntervalVector& y_box) {
//    cout<<"handle constraint on xy, xy init: "<<xy_box<<endl;
        switch (check_constraints(xy_box)){
            case 2: { // all the constraints are satisfied
                data_y->pu=1;
                break;
            }
            case 0: { // One constraint is false
                // constraint on x and y not respected, move on.
                return true;
            }
            default: // nothing to do
                break;
        }

        if (data_y->pu != 1)  {// there is a constraint on x and y
            ctc_xy.contract(xy_box);
            if (xy_box.is_empty()) { // constraint on x and y not respected, move on.
                return true;
            } else {
                // TODO to check normalement on peut propager la contraction sur le y
                for (int k=0; k<y_box.size(); k++) {
                    //y_cell->box[k] &= xy_box[x_cell->box.size()+k];
                    //y_box[k] &= xy_box[xy_box.size()-y_box.size()-1+k];
                    y_box[k] &= xy_box[xy_box.size()-y_box.size()+k];
                }
            }
        }
        return false;
    }


    bool EvalMax::handle_cstfree(IntervalVector& xy_box, Cell* const y_cell) {
        //    std::cout<<"init box: "<<*xy_box<<std::endl;
//        if((xy_box.subvector(xy_box.size()-y_cell->box.size(), xy_box.size()-1)).max_diam()<=1.e-14)
//                return true;
        IntervalVector grad(xy_box.size());
        xy_sys.goal->gradient(xy_box,grad);
        for (int i=xy_box.size()-y_cell->box.size(); i<xy_box.size(); i++) {
            //        //        std::cout<<"i = "<<i<<std::endl;
            if (grad[i].lb() > 0) {
//                        (xy_box)[i] = Interval((xy_box)[i].ub() -1.e-15,(xy_box)[i].ub());
                (xy_box)[i] = Interval((xy_box)[i].ub());
            }
            if (grad[i].ub() < 0) {
//                        (xy_box)[i] = Interval((xy_box)[i].lb(),(xy_box)[i].lb()+1.e-15);
                (xy_box)[i] = Interval((xy_box)[i].lb());
            }
        }
        //    std::cout<<"final box: "<<*xy_box<<std::endl;
        //    std::cout<<"free cst contraction done, contracted box: "<<*xy_box<<std::endl;
        return true;
    }

    IntervalVector EvalMax::get_feasible_point(Cell* x_cell, Cell* const y_cell) {
        IntervalVector mid_y_box = get_mid_y(x_cell->box,y_cell->box); // get the box (x,mid(y))
        auto y_data = dynamic_cast<BxpMinMax*>(y_cell->prop[BxpMinMax::get_id(*this)]);
        if (!y_data)
            ibex_error("[ibex_EvalMax] -- null ptr in get_feasible_point: y_data element has no BxpMinMax.");
        if ((y_data->pu != 1)) { // constraint on xy exist and is not proved to be satisfied
            int res = check_constraints(mid_y_box);
            if (res == 0 ||res == 1)
                return {1, Interval::EMPTY_SET};
        }
        return mid_y_box;
    }

    Interval EvalMax::eval_all(Function* f, const IntervalVector& box) {
        Interval eval = f->eval(box);
//    cout<<"natural eval: "<<eval<<endl;
//    Interval eval_centered =  f->eval_centered(box);
//    cout<<"centered eval: "<<eval_centered<<endl;
//    Interval eval_baumann = f->eval_baumann(box);
//    cout<<"baumann eval: "<<eval_baumann<<endl;
//    eval &= eval_centered;
//    eval &= eval_baumann;
//    cout<<"eval intersect: "<<eval<<endl;
//    return eval_baumann;
//     Interval itv = affine_goal->eval(box).i();
        return eval;
//     return itv;
    }


    int EvalMax::check_constraints(const IntervalVector& xy_box) {
        int res = 2;

        for (int i=0; i<xy_sys.nb_ctr; i++) {
            Interval int_res = eval_all(const_cast<Function *>(&(xy_sys.ctrs[i].f)), xy_box);
            //            cout<<" val of ctr "<<i<<" : "<<int_res<<endl;
            if (int_res.lb() > 0)
                return 0;
            else if (int_res.ub() >= 0)
                res = 1;
        }
        return res;
    }

    IntervalVector EvalMax::get_mid_y(const IntervalVector& x_box, const IntervalVector& y_box) { // returns the cast of box x and mid of box y
        IntervalVector res(y_box.size()+x_box.size());
        for (int i=0; i<x_box.size(); i++) {
            res[i] = x_box[i];
        }
        for (int i=0; i<y_box.size(); i++) {
            res[x_box.size()+i] = y_box[i].mid();
        }
        return res;
    }

    void export_monitor(vector<double>* ub, vector<double>* lb, vector<double>* nbel, vector<double>* nbel_save, const IntervalVector& box) {
        //    std::ofstream out;
        ofstream out("log.txt", ios::app);
        //    std::string output_name = "log.txt";
        //    out.open("log.txt");
        //    out<<"x box: "<<box<<std::endl;
        for (int i = 0; i<ub->size(); i++) {
            out<<ub->at(i)<<" "<<lb->at(i)<<" "<<nbel->at(i)<<" "<<nbel_save->at(i)<<std::endl;
        }
        out.close();
    }

} // end namespace ibex