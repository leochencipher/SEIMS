/*!
 * \brief Define some common used function in channel routing related modules, e.g., MUSK_CH.
 * \author Liang-Jun Zhu
 * \date 2018-8-11
 */
#ifndef SEIMS_CHANNEL_ROUTING_COMMON_H
#define SEIMS_CHANNEL_ROUTING_COMMON_H

/*!
 * \brief Calculates flow rate or flow velocity using Manning's
 *        equation. If x1 is set to 1, the velocity is calculated. If x1 is set to
 *        cross-sectional area of flow, the flow rate is calculated.
 *        Refers code Qman.f in SWAT.
 * \param[in] x1 cross-sectional flow area or 1, m^2 or none.
 * \param[in] x2 hydraulic radius, m.
 * \param[in] x3 Manning's "n" value for channel.
 * \param[in] x4 average slope of channel, m/m.
 * \return flow rate or flow velocity, m^3/s or m/s.
 */
float manningQ(float x1, float x2, float x3, float x4);

/*!
 * \brief Calculate channel bottom width by channel width, side slope, and depth.
 *        Refers code ttcoef.f in SWAT.
 * \param[in] ch_wth Channel upper width
 * \param[inout] ch_sideslp The inverse of channel side slope (default is 2, slope = 0.5), which maybe updated when bottom width < 0
 * \param[inout] ch_depth Channel depth, which maybe updated when bottom width < 0
 * \return Channel bottom width
 */
float ChannleBottomWidth(float ch_wth, float& ch_sideslp, float& ch_depth);

/*!
 * \brief Channel wetting perimeter for both floodplain and not full channel
 * \param[in] ch_btmwth Channel bottom width
 * \param[in] ch_depth Channel depth
 * \param[in] wtr_depth Channel water depth
 * \param[in] ch_sideslp The inverse of channel side slope (default is 2, slope = 0.5)
 * \param[in] ch_wth Channel width at bankfull
 * \param[in] fps The inverse of floodplain side slope (default is 4, slope = 0.25)
 * \return Channel wetting perimeter
 */
float ChannelWettingPerimeter(float ch_btmwth, float ch_depth, float wtr_depth,
                              float ch_sideslp, float ch_wth, float fps = 4.f);

/*!
 * \brief Channel wetting perimeter for not full channel
 * \param[in] ch_btmwth Channel bottom width
 * \param[in] wtr_depth Channel water depth
 * \param[in] ch_sideslp The inverse of channel side slope (default is 2, slope = 0.5)
 * \return Channel wetting perimeter
 */
float ChannelWettingPerimeter(float ch_btmwth, float wtr_depth, float ch_sideslp);

/*!
 * \brief Cross-sectional area of channel for both floodplain and not full channel
 * \param[in] ch_btmwth Channel bottom width
 * \param[in] ch_depth Channel depth
 * \param[in] wtr_depth Channel water depth
 * \param[in] ch_sideslp The inverse of channel side slope (default is 2, slope = 0.5)
 * \param[in] ch_wth Channel width at bankfull
 * \param[in] fps The inverse of floodplain side slope (default is 4, slope = 0.25)
 * \return Channel cross-sectional area
 */
float ChannelCrossSectionalArea(float ch_btmwth, float ch_depth, float wtr_depth,
                                float ch_sideslp, float ch_wth, float fps = 4.f);

/*!
 * \brief Cross-sectional area of channel for not full channel
 * \param[in] ch_btmwth Channel bottom width
 * \param[in] wtr_depth Channel water depth
 * \param[in] ch_sideslp The inverse of channel side slope (default is 2, slope = 0.5)
 * \return Channel cross-sectional area
 */
float ChannelCrossSectionalArea(float ch_btmwth, float wtr_depth, float ch_sideslp);

/*!
 * \brief Compute storage time constant for channel (ratio of storage to discharge)
 * \param[in] ch_manning Manning's n value of channel
 * \param[in] ch_slope Channel slope
 * \param[in] ch_len Channel length, m
 * \param[in] radius Hydraulic radius, m
 * \return Storage time constant
 */
float StorageTimeConstant(float ch_manning, float ch_slope, float ch_len,
                          float radius);

#endif /* SEIMS_CHANNEL_ROUTING_COMMON_H */
