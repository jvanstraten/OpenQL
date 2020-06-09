/**
 * @file   circuit.h
 * @date   11/2016
 * @author Nader Khammassi
 * @brief  circuit (i.e. gate container) implementation
 */

#ifndef CIRCUIT_H
#define CIRCUIT_H

#include <vector>
#include <iostream>
#include <memory>

#include "gate.h"

namespace ql
{

class circuit : public std::vector<std::shared_ptr<gate>> {
public:
    void print() const
    {
        std::cout << "-------------------" << std::endl;
        for (auto &g : *this)
        {
            std::cout << "   " << g->qasm() << std::endl;
        }
        std::cout << "\n-------------------" << std::endl;
    }

    /**
     * generate qasm for a given circuit
     */
    std::string qasm() const
    {
        std::stringstream ss;
        for (auto &g : *this)
        {
            ss << g->qasm() << "\n";
        }
        return ss.str();
    }

    std::vector<circuit> split_circuit() const
    {
        IOUT("circuit decomposition in basic blocks ... ");
        std::vector<circuit> cs;
        cs.emplace_back();
        for (auto &g: *this)
        {
            if ((g->type() == __prepz_gate__) || (g->type() == __measure_gate__))
            {
                cs.emplace_back();
                cs.back().push_back(g);
                cs.emplace_back();
            }
            else
            {
                cs.back().push_back(g);
            }
        }
        IOUT("circuit decomposition done (" << cs.size() << ").");
        /*
           for (int i=0; i<cs.size(); ++i)
           {
           println(" |-- circuit " << i);
           print(*(cs[i]));
           }
         */
        return cs;
    }

    /**
     * detect measurements and qubit preparations
     */
    bool contains_measurements() const
    {
        for (auto &g : *this)
        {
            if (g->type() == __measure_gate__)
                return true;
            if (g->type() == __prepz_gate__)
                return true;
        }
        return false;
    }

};

}

#endif // CIRCUIT_H
