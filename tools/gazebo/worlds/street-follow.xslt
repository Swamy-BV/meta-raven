<?xml version="1.0"?>
<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Reuse the street assets; change only the flight sensors, camera and target route. -->
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="xml" indent="yes"/>
  <xsl:template match="@*|node()"><xsl:copy><xsl:apply-templates select="@*|node()"/></xsl:copy></xsl:template>
  <xsl:template match="world/@name"><xsl:attribute name="name">street_follow</xsl:attribute></xsl:template>
  <xsl:template match="world">
    <xsl:copy><xsl:apply-templates select="@*|node()"/>
      <gravity>0 0 -9.81</gravity>
      <magnetic_field>6e-06 2.3e-05 -4.2e-05</magnetic_field>
      <plugin filename="gz-sim-imu-system" name="gz::sim::systems::Imu"/>
      <plugin filename="gz-sim-air-pressure-system" name="gz::sim::systems::AirPressure"/>
      <plugin filename="gz-sim-magnetometer-system" name="gz::sim::systems::Magnetometer"/>
      <plugin filename="gz-sim-navsat-system" name="gz::sim::systems::NavSat"/>
      <spherical_coordinates><surface_model>EARTH_WGS84</surface_model><world_frame_orientation>ENU</world_frame_orientation><latitude_deg>47.397971057728974</latitude_deg><longitude_deg>8.546163739800146</longitude_deg><elevation>0</elevation></spherical_coordinates>
    </xsl:copy>
  </xsl:template>
  <!-- Match PX4's reference Gazebo world: 250 Hz flight physics. -->
  <xsl:template match="physics">
    <physics type="ode"><max_step_size>0.004</max_step_size><real_time_factor>1</real_time_factor><real_time_update_rate>250</real_time_update_rate></physics>
  </xsl:template>
  <xsl:template match="model[@name='camera']">
    <include><uri>model://follow_drone</uri><name>x500_1</name><pose>-4 -11 0.24 0 0 1.57079632679</pose></include>
  </xsl:template>
  <xsl:template match="actor[@name='moving_car']/script/trajectory">
    <trajectory id="0" type="drive" tension="1">
      <waypoint><time>0</time><pose>-4 -1.5 0.05 0 0 0</pose></waypoint>
      <waypoint><time>25</time><pose>-4 -1.5 0.05 0 0 0</pose></waypoint>
      <waypoint><time>65</time><pose>12 -1.5 0.05 0 0 0</pose></waypoint>
      <waypoint><time>69</time><pose>13 0 0.05 0 0 1.57079632679</pose></waypoint>
      <waypoint><time>73</time><pose>12 1.5 0.05 0 0 3.14159265359</pose></waypoint>
      <waypoint><time>133</time><pose>-12 1.5 0.05 0 0 3.14159265359</pose></waypoint>
      <waypoint><time>137</time><pose>-13 0 0.05 0 0 4.71238898038</pose></waypoint>
      <waypoint><time>141</time><pose>-12 -1.5 0.05 0 0 6.28318530718</pose></waypoint>
      <waypoint><time>161</time><pose>-4 -1.5 0.05 0 0 6.28318530718</pose></waypoint>
    </trajectory>
  </xsl:template>
</xsl:stylesheet>
