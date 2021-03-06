#include "Header.h"
#include "Other.h"

namespace NPSMLE
{
	template<typename GeneratorType = std::mt19937_64, typename GeneratorSeed = RandomStart>
	void simulate_joint_process(double * price, double * volatility, double * sentiment, JointParameters *
		parameters, double dt, int N_obs, int M_obs, double p0, double v0)
	{
		// Unpack simulation parameters
		double mu_p = parameters->mu_p;
		double gamma_p = parameters->gamma_p;
		double gamma_v = parameters->gamma_v;
		double mu_v = parameters->mu_v;
		double beta_v = parameters->beta_v;
		double sigma_v = parameters->sigma_v;
		double rho_pv = parameters->rho_pv;

		// Pre-allocate variables
		double mv, sv, mp, sp;
		double delta = dt / M_obs;
		double sqrt_delta = sqrt(delta);

		// Allocate space for correlated Wiener processes
		double W_p, W_v;

		// Initialize random engines
		GeneratorType generator;
		generator.seed(GeneratorSeed()());
		std::normal_distribution<double> distribution(0.0, 1.0);

		// Initialize first values
		volatility[0] = v0;
		price[0] = p0;

		// Process generation
		for (int i = 1; i != N_obs; ++i)
		{
			volatility[i] = volatility[i - 1];
			price[i] = price[i - 1];

			for (int j = 0; j != M_obs; ++j)
			{
				W_v = distribution(generator);
				W_p = sqrt(1.0 - rho_pv * rho_pv) * distribution(generator) + rho_pv * W_v;

				mp = gamma_p * (mu_p - price[i]);
				sp = price[i] * sqrt(abs(volatility[i]));
				price[i] += mp * delta + W_p * sp * sqrt_delta;

				mv = gamma_v * (mu_v + beta_v * abs(sentiment[i]) - volatility[i]);
				sv = sigma_v * sqrt(abs(volatility[i]));
				volatility[i] += mv * delta + W_v * sv * sqrt_delta;
			}
		}
	}

	template<typename GeneratorType = std::mt19937_64, typename GeneratorSeed = RandomStart>
	double simulated_ll_joint(const std::vector<double>& x, std::vector<double>& grad, void* data)
	{
		// Unwraping parameter
		double gamma_p = x[0];
		double mu_p = x[1];
		double gamma_v = x[2];
		double mu_v = x[3];
		double beta_v = x[4];
		double sigma_v = x[5];
		double rho_pv = x[6];

		// Unwraping data
		WrapperSimulatedJoint<GeneratorType, GeneratorSeed> *wrapper =
			static_cast<WrapperSimulatedJoint<GeneratorType, GeneratorSeed>*>(data);
		double *price = wrapper->price;
		double *volatility = wrapper->volatility;
		double *sentiment = wrapper->interpolated_sentiment;
		double *simulated_price = wrapper->simulated_price;
		double *simulated_volatility = wrapper->simulated_volatility;
		double *random_buffer_price = wrapper->random_buffer_price;
		double *random_buffer_volatility = wrapper->random_buffer_volatility;
		int N_obs = wrapper->N_obs;
		int N_sim = wrapper->N_sim;
		int M_sim = wrapper->M_sim;
		double dt = wrapper->dt;

		// Pre-allocating variables
		double ll = 0.0;
		double kernel_sum_price = 0.0, kernel_sum_volatility = 0.0, kernel_sum = 0.0;
		const int dimy = 1;
		const double sqrt_pi = sqrt(2.0 * M_PI);
		const double undersmooth = 0.5;
		const double h_frac = pow(4.0 / dimy + 2.0, 1.0 / (dimy + 4.0)) * pow(N_sim, -(1.0 + undersmooth) / (dimy + 4.0));
		const double delta = dt / M_sim;
		const double sqrt_delta = sqrt(delta);
		double mv, mp, sqrt_vol;
		double h_price, h_volatility;

		// Fill correlated Wiener process buffers
		double * W_v = wrapper->wiener_volatility;
		double * W_p = wrapper->wiener_price;

		for (int i = 0; i != N_sim * M_sim; ++i)
		{
			W_v[i] = random_buffer_volatility[i];
			W_p[i] = sqrt(1.0 - rho_pv * rho_pv) * random_buffer_price[i] + rho_pv * W_v[i];
		}

		// Main log-likelihood computation
		for (int i = 1; i != N_obs; ++i)
		{
			for (int j = 0; j != N_sim; ++j)
			{
				simulated_price[j] = price[i - 1];
				simulated_volatility[j] = volatility[i - 1];

				for (int k = 0; k != M_sim; ++k)
				{
					mp = gamma_p * (mu_p - simulated_price[j]);
					sqrt_vol = simulated_price[j] * sqrt(abs(simulated_volatility[j]));
					simulated_price[j] += mp * delta + W_p[j * M_sim + k] * sqrt_vol * sqrt_delta;

					mv = gamma_v * (mu_v + beta_v * abs(sentiment[(i - 1) * M_sim + k]) - simulated_volatility[j]);
					simulated_volatility[j] += mv * delta + W_v[j * M_sim + k] * sqrt_vol * sigma_v * sqrt_delta;
				}
			}

			// Optimal kernel bandwidth computation
			h_price = h_frac * st_dev(simulated_price, N_sim);
			h_volatility = h_frac * st_dev(simulated_volatility, N_sim);

			for (int j = 0; j != N_sim; ++j)
			{
				kernel_sum_price = exp((-(simulated_price[j] - price[i]) * (simulated_price[j] - price[i])) / (2.0 * h_price * h_price)) / (h_price * sqrt_pi);
				kernel_sum_volatility = exp((-(simulated_volatility[j] - volatility[i]) * (simulated_volatility[j] - volatility[i])) / (2.0 * h_volatility * h_volatility)) / (h_volatility * sqrt_pi);
				kernel_sum += kernel_sum_volatility * kernel_sum_price;
			}

			ll += ::log(kernel_sum / N_sim);

			kernel_sum = 0.0;

#ifdef INFINITY_CHECK
			// Speed up in cases of infinity
			if (ll == -INFINITY || !std::isnormal(ll))
			{
				return max_double;
			}
#endif
		}

		return -ll;
	}
}