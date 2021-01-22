#ifndef HISTOGRAM_LIHEAR_H
#define HISTOGRAM_LIHEAR_H

#include <algorithm> /* max, find_if_not */
#include <cstddef> /* size_t */
#include <memory> /* shared_ptr */
#include <numeric> /* accumulate */
#include <utility> /* pair */
#include <vector>

template <typename T>
	class histogram
	{
	public:
		using data_t = std::vector<T>;
	private:
		data_t _hist;
		T      _out_of_range;
		double _incr;
		/*
		 * The multiplier necessary to scale "enter" arguement, in nanoseconds, to the range of a single element
		 */
		double _scale;
	public:
		histogram(std::size_t size_, double incr_)
			: _hist(size_, T{0})
			, _out_of_range(T{0})
			, _incr(incr_)
			, _scale{1.0/incr_}
		{}
		void enter(double t)
		{
			const auto slot = static_cast<std::size_t>(std::max(0.0,t * _scale));
			++(slot < _hist.size() ? _hist[slot] : _out_of_range);
		}
		const data_t &hist() const { return _hist; }
		const T count_captured() const { return std::accumulate(_hist.begin(), _hist.end(), T{0}); }
		const T count_lost() const { return _out_of_range; }
		const T count_all() const { return count_captured() + count_lost(); }
		double capture_fraction() const { return double(count_captured())/double(count_all()); }
		typename data_t::const_iterator end_non_zero() const
		{
		return
			std::find_if_not(
				this->hist().rbegin()
				, this->hist().rend()
				, [](const auto v) { return v == 0; }
			).base();
		}

		std::pair<double, double> captured_mean_range() const
		{
			auto hist_total_min = 0.0;
			auto hist_total_mac = 0.0;
			auto bucket_low = 0.0;
			for ( auto i = this->hist().begin(); i != this->hist().end(); ++i, bucket_low += _incr )
			{
				hist_total_min += double(*i) * bucket_low;
				hist_total_mac += double(*i) * (bucket_low + _incr);
			}
			return { hist_total_min/double(count_captured()), hist_total_mac/double(count_captured()) };
		}
	};

template <typename T>
	struct linear_histogram_factory
	{
		std::size_t _size;
		double      _incr; /* in nanoseconds */
	public:
		using histogram_t = histogram<T>;
		explicit linear_histogram_factory(std::size_t size_, double incr_)
			: _size(size_)
			, _incr(incr_)
		{
			if ( 0 == incr() ) {
				throw std::domain_error{"Histogram increment is zero."};
			}
		}
		std::shared_ptr<histogram_t> make() const
		{
			return std::make_shared<histogram<T>>(_size, double(incr()));
		}
		double incr() const { return _incr; }
	};

#endif
