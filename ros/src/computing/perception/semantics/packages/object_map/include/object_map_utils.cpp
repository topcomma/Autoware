/*
 *  Copyright (c) 2018, Nagoya University
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of Autoware nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ********************
 *
 */

#include "object_map_utils.hpp"

namespace object_map
{
	geometry_msgs::Point TransformPoint(const geometry_msgs::Point &in_point, const tf::Transform &in_tf)
	{
		tf::Point tf_point;
		tf::pointMsgToTF(in_point, tf_point);

		tf_point = in_tf * tf_point;

		geometry_msgs::Point out_point;
		tf::pointTFToMsg(tf_point, out_point);

		return out_point;
	}

	void PublishGridMap(const grid_map::GridMap &in_gridmap, const ros::Publisher &in_publisher)
	{
		grid_map_msgs::GridMap message;
		grid_map::GridMapRosConverter::toMessage(in_gridmap, message);
		in_publisher.publish(message);
	}

	void PublishOccupancyGrid(const grid_map::GridMap &in_gridmap,
	                          const ros::Publisher &in_publisher,
	                          const std::string& in_layer,
	                          double in_min_value,
	                          double in_max_value)
	{
		nav_msgs::OccupancyGrid message;
		grid_map::GridMapRosConverter::toOccupancyGrid(in_gridmap, in_layer, in_min_value, in_max_value, message );
		in_publisher.publish(message);
	}

	tf::StampedTransform FindTransform(const std::string &in_target_frame, const std::string &in_source_frame,
	                                   const tf::TransformListener &in_tf_listener)
	{
		tf::StampedTransform transform;

		try
		{
			in_tf_listener.lookupTransform(in_target_frame, in_source_frame, ros::Time(0), transform);
		}
		catch (tf::TransformException ex)
		{
			ROS_ERROR("%s", ex.what());
		}

		return transform;
	}

	std::vector<geometry_msgs::Point>
	SearchAreaPoints(const vector_map::Area &in_area, const vector_map::VectorMap &in_vectormap)
	{
		std::vector<geometry_msgs::Point> area_points;
		std::vector<geometry_msgs::Point> area_points_empty;

		if (in_area.aid == 0)
			return area_points_empty;

		vector_map_msgs::Line line = in_vectormap.findByKey(vector_map::Key<vector_map_msgs::Line>(in_area.slid));
		// must set beginning line
		if (line.lid == 0 || line.blid != 0)
			return area_points_empty;

		// Search all lines in in_area
		while (line.flid != 0)
		{
			vector_map_msgs::Point bp = in_vectormap.findByKey(vector_map::Key<vector_map_msgs::Point>(line.bpid));
			if (bp.pid == 0)
				return area_points_empty;

			vector_map_msgs::Point fp = in_vectormap.findByKey(vector_map::Key<vector_map_msgs::Point>(line.fpid));
			if (fp.pid == 0)
				return area_points_empty;

			// 2 points of line
			area_points.push_back(vector_map::convertPointToGeomPoint(bp));
			area_points.push_back(vector_map::convertPointToGeomPoint(fp));

			line = in_vectormap.findByKey(vector_map::Key<vector_map_msgs::Line>(line.flid));
			if (line.lid == 0)
				return area_points_empty;
		}

		vector_map_msgs::Point bp = in_vectormap.findByKey(vector_map::Key<vector_map_msgs::Point>(line.bpid));
		vector_map_msgs::Point fp = in_vectormap.findByKey(vector_map::Key<vector_map_msgs::Point>(line.fpid));
		if (bp.pid == 0 || fp.pid == 0)
			return area_points_empty;

		area_points.push_back(vector_map::convertPointToGeomPoint(bp));
		area_points.push_back(vector_map::convertPointToGeomPoint(fp));

		return area_points;
	}

	void FillPolygonAreas(grid_map::GridMap &out_grid_map, const std::vector<std::vector<geometry_msgs::Point>> &in_area_points,
		                      const std::string &in_grid_layer_name, const int in_layer_background_value,
		                      const int in_layer_min_value, const int in_fill_color, const int in_layer_max_value,
		                      const std::string &in_tf_target_frame, const std::string &in_tf_source_frame,
		                      const tf::TransformListener &in_tf_listener)
	{
		if(!out_grid_map.exists(in_grid_layer_name))
		{
			out_grid_map.add(in_grid_layer_name);
		}
		out_grid_map[in_grid_layer_name].setConstant(in_layer_background_value);

		cv::Mat original_image;
		grid_map::GridMapCvConverter::toImage<unsigned char, 1>(out_grid_map,
		                                                        in_grid_layer_name,
		                                                        CV_8UC1,
		                                                        in_layer_min_value,
		                                                        in_layer_max_value,
		                                                        original_image);

		cv::Mat filled_image = original_image.clone();

		tf::StampedTransform tf = FindTransform(in_tf_target_frame, in_tf_source_frame, in_tf_listener);

		// calculate out_grid_map position
		grid_map::Position map_pos = out_grid_map.getPosition();
		double origin_x_offset = out_grid_map.getLength().x() / 2.0 - map_pos.x();
		double origin_y_offset = out_grid_map.getLength().y() / 2.0 - map_pos.y();

		for (const auto &points : in_area_points)
		{
			std::vector<cv::Point> cv_points;

			for (const auto &p : points)
			{
				// transform to GridMap coordinate
				geometry_msgs::Point tf_point = TransformPoint(p, tf);

				// coordinate conversion for cv image
				double cv_x = (out_grid_map.getLength().y() - origin_y_offset - tf_point.y) / out_grid_map.getResolution();
				double cv_y = (out_grid_map.getLength().x() - origin_x_offset - tf_point.x) / out_grid_map.getResolution();
				cv_points.emplace_back(cv::Point(cv_x, cv_y));
			}

			cv::fillConvexPoly(filled_image, cv_points.data(), cv_points.size(), cv::Scalar(in_fill_color));
		}

		// convert to ROS msg
		grid_map::GridMapCvConverter::addLayerFromImage<unsigned char, 1>(filled_image,
		                                                                  in_grid_layer_name,
		                                                                  out_grid_map,
		                                                                  in_layer_min_value,
		                                                                  in_layer_max_value);
	}

  void addLayerFromImageWithoutNormValues(const cv::Mat& image, const std::string &in_grid_layer_name, grid_map::GridMap& grid_map)
  {
    grid_map.add(in_grid_layer_name);
    grid_map::Matrix& data = grid_map[in_grid_layer_name];

    for (grid_map::GridMapIterator iterator(grid_map); !iterator.isPastEnd(); ++iterator)
    {
      const grid_map::Index index(*iterator);
      // Compute value.
      const float image_value = image.at<float>(index(0), index(1));
      // const float mapValue = lowerValue + mapValueDifference * ((float) imageValue / maxImageValue);
      data(index(0), index(1)) = image_value;
    }
  }

  void FillPolygonLaneAreas(grid_map::GridMap &out_grid_map, const std::vector<std::vector<geometry_msgs::Point>> &in_area_points,
		                      const std::string &in_grid_layer_name, const int in_layer_background_value,
		                      const int in_layer_min_value, const int in_layer_max_value,
		                      const std::string &in_tf_target_frame, const std::string &in_tf_source_frame,
		                      const tf::TransformListener &in_tf_listener)
	{
		if(!out_grid_map.exists(in_grid_layer_name))
		{
			out_grid_map.add(in_grid_layer_name);
		}
		out_grid_map[in_grid_layer_name].setConstant(in_layer_background_value);

		cv::Mat original_image;
		grid_map::GridMapCvConverter::toImage<float, 1>(out_grid_map,
		                                                        in_grid_layer_name,
		                                                        CV_32FC1,
		                                                        in_layer_min_value,
		                                                        in_layer_max_value,
		                                                        original_image);
    original_image.setTo(cv::Scalar(in_layer_background_value));
		cv::Mat filled_image = original_image.clone();
    // std::cout << "M = "<< std::endl << " "  << original_image << std::endl << std::endl;

		tf::StampedTransform tf = FindTransform(in_tf_target_frame, in_tf_source_frame, in_tf_listener);

		// calculate out_grid_map position
		grid_map::Position map_pos = out_grid_map.getPosition();
		double origin_x_offset = out_grid_map.getLength().x() / 2.0 - map_pos.x();
		double origin_y_offset = out_grid_map.getLength().y() / 2.0 - map_pos.y();

		for (const auto &points : in_area_points)
		{
			std::vector<cv::Point> cv_points;

      double height = 0;
			for (const auto &p : points)
			{
				// transform to GridMap coordinate
				geometry_msgs::Point tf_point = TransformPoint(p, tf);

				// coordinate conversion for cv image
				double cv_x = (out_grid_map.getLength().y() - origin_y_offset - tf_point.y) / out_grid_map.getResolution();
				double cv_y = (out_grid_map.getLength().x() - origin_x_offset - tf_point.x) / out_grid_map.getResolution();
				cv_points.emplace_back(cv::Point(cv_x, cv_y));
        height = tf_point.z;
			}
      // std::cout << "map height " << points[0].z << std::endl;
      // std::cout << "height "<< height << std::endl;
			cv::fillConvexPoly(filled_image, cv_points.data(), cv_points.size(), height);
		}
    // std::cout << "M = "<< std::endl << " "  << filled_image << std::endl << std::endl;

		// convert to ROS msg
		// grid_map::GridMapCvConverter::addLayerFromImage<float, 1>(filled_image,
		//                                                                   in_grid_layer_name,
		//                                                                   out_grid_map,
		//                                                                   in_layer_min_value,
		//                                                                   in_layer_max_value);
    // std::cout << out_grid_map << std::endl;
    addLayerFromImageWithoutNormValues(filled_image, in_grid_layer_name, out_grid_map);
	}

	void LoadRoadAreasFromVectorMap(ros::NodeHandle& in_private_node_handle,
	                                std::vector<std::vector<geometry_msgs::Point>>& out_area_points)
	{
		vector_map::VectorMap vmap;
		vmap.subscribe(in_private_node_handle,
		               vector_map::Category::POINT | vector_map::Category::LINE | vector_map::Category::AREA |
		               vector_map::Category::WAY_AREA, 10);

		std::vector<vector_map_msgs::WayArea> way_areas =
				vmap.findByFilter([](const vector_map_msgs::WayArea &way_area)
				                  {
					                  return true;
				                  });

		if (way_areas.empty())
		{
			ROS_WARN_STREAM("No WayArea...");
			return;
		}

		for (const auto &way_area : way_areas)
		{
			vector_map_msgs::Area area = vmap.findByKey(vector_map::Key<vector_map::Area>(way_area.aid));
			out_area_points.emplace_back(SearchAreaPoints(area, vmap));
		}

	}

} // namespace object_map
